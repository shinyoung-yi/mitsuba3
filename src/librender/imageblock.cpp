#include <mitsuba/render/imageblock.h>
#include <mitsuba/core/bitmap.h>
#include <mitsuba/core/profiler.h>
#include <enoki/loop.h>

NAMESPACE_BEGIN(mitsuba)

MTS_VARIANT
ImageBlock<Float, Spectrum>::ImageBlock(const ScalarVector2u &size,
                                        uint32_t channel_count,
                                        const ReconstructionFilter *rfilter,
                                        bool border, bool normalize,
                                        bool coalesce, bool warn_negative,
                                        bool warn_invalid)
    : m_offset(0), m_size(0), m_channel_count(channel_count),
      m_rfilter(rfilter), m_normalize(normalize), m_coalesce(coalesce),
      m_warn_negative(warn_negative), m_warn_invalid(warn_invalid) {

    // Detect if a box filter is being used, and discard m_rfilter in that case
    if (rfilter && rfilter->radius() == .5f)
        m_rfilter = nullptr;

    // Determine the size of the boundary region from the reconstruction filter
    m_border_size = (m_rfilter && border) ? m_rfilter->border_size() : 0u;

    // Allocate memory for the image tensor
    set_size(size);
}

MTS_VARIANT
ImageBlock<Float, Spectrum>::ImageBlock(const TensorXf &tensor,
                                        const ReconstructionFilter *rfilter,
                                        bool border, bool normalize,
                                        bool coalesce, bool warn_negative,
                                        bool warn_invalid)
    : m_offset(0), m_rfilter(rfilter), m_normalize(normalize),
      m_coalesce(coalesce), m_warn_negative(warn_negative),
      m_warn_invalid(warn_invalid) {

	if (tensor.ndim() != 3)
		Throw("ImageBlock(const TensorXf&): expected a 3D tensor (height x width x channels)!");

    // Detect if a box filter is being used, and discard m_rfilter in that case
    if (rfilter && rfilter->radius() == .5f)
        m_rfilter = nullptr;

    // Determine the size of the boundary region from the reconstruction filter
    m_border_size = (m_rfilter && border) ? m_rfilter->border_size() : 0u;

    m_size = ScalarVector2u((uint32_t) tensor.shape(1), (uint32_t) tensor.shape(0));
    m_channel_count = (uint32_t) tensor.shape(2);

    // Account for the boundary region, if present
    if (border && ek::any(m_size < 2 * m_border_size))
		Throw("ImageBlock(const TensorXf&): image is too small to have a boundary!");
	m_size -= 2 * m_border_size;

    // Copy the image tensor
    if constexpr (ek::is_jit_array_v<Float>)
        m_tensor = TensorXf(tensor.array().copy(), 3, tensor.shape().data());
    else
        m_tensor = TensorXf(tensor.array(), 3, tensor.shape().data());
}

MTS_VARIANT ImageBlock<Float, Spectrum>::~ImageBlock() { }

MTS_VARIANT void ImageBlock<Float, Spectrum>::clear() {
    using Array = typename TensorXf::Array;

    ScalarVector2u size_ext = m_size + 2 * m_border_size;

    size_t size_flat = m_channel_count * ek::hprod(size_ext),
           shape[3]  = { size_ext.y(), size_ext.x(), m_channel_count };

    m_tensor = TensorXf(ek::zero<Array>(size_flat), 3, shape);
}

MTS_VARIANT void
ImageBlock<Float, Spectrum>::set_size(const ScalarVector2u &size) {
    using Array = typename TensorXf::Array;

    if (size == m_size)
        return;

    ScalarVector2u size_ext = m_size + 2 * m_border_size;

    size_t size_flat = m_channel_count * ek::hprod(size_ext),
           shape[3]  = { size_ext.y(), size_ext.x(), m_channel_count };

    m_tensor = TensorXf(ek::zero<Array>(size_flat), 3, shape);
    m_size = size;
}

MTS_VARIANT void ImageBlock<Float, Spectrum>::put_block(const ImageBlock *block) {
    ScopedPhase sp(ProfilerPhase::ImageBlockPut);

    if (unlikely(block->channel_count() != channel_count()))
        Throw("ImageBlock::accum_block(): mismatched channel counts!");

    ScalarVector2i source_size   = block->size() + 2 * block->border_size(),
                   target_size   =        size() + 2 *        border_size();

    ScalarPoint2i  source_offset = block->offset() - block->border_size(),
                   target_offset =        offset() -        border_size();

    if constexpr (ek::is_jit_array_v<Float>) {
        // If target block is cleared and match size, directly copy data
        if (m_tensor.array().is_literal() && m_tensor.array()[0] == 0.f &&
            m_size == block->size() && m_offset == block->offset() &&
            m_border_size == block->border_size()) {
            m_tensor.array() = block->tensor().array().copy();
        } else {
            accumulate_2d<Float &, const Float &>(
                block->tensor().array(), source_size,
                m_tensor.array(), target_size,
                ScalarVector2i(0), source_offset - target_offset,
                source_size, channel_count()
            );
        }
    } else {
        accumulate_2d(
            block->tensor().data(), source_size,
            m_tensor.data(), target_size,
            ScalarVector2i(0), source_offset - target_offset,
            source_size, channel_count()
        );
    }
}

MTS_VARIANT void ImageBlock<Float, Spectrum>::put(const Point2f &pos,
                                                  const Float *values,
                                                  Mask active) {
    ScopedPhase sp(ProfilerPhase::ImageBlockPut);
    constexpr bool JIT = ek::is_jit_array_v<Float>;

    // Check if all sample values are valid
    if (m_warn_negative || m_warn_invalid) {
        Mask is_valid = true;

        if (m_warn_negative) {
            for (uint32_t k = 0; k < m_channel_count; ++k)
                is_valid &= values[k] >= -1e-5f;
        }

        if (m_warn_invalid) {
            for (uint32_t k = 0; k < m_channel_count; ++k)
                is_valid &= ek::isfinite(values[k]);
        }

        if (unlikely(ek::any(active && !is_valid))) {
            std::ostringstream oss;
            oss << "Invalid sample value: [";
            for (uint32_t i = 0; i < m_channel_count; ++i) {
                oss << values[i];
                if (i + 1 < m_channel_count) oss << ", ";
            }
            oss << "]";
            Log(Warn, "%s", oss.str());
        }
    }

    // ===================================================================
    //  Fast special case for the box filter
    // ===================================================================

    if (!m_rfilter) {
        Point2u p = Point2u(ek::floor2int<Point2i>(pos) - ScalarPoint2i(m_offset));

        // Switch over to unsigned integers, compute pixel index
        UInt32 index = ek::fmadd(p.y(), m_size.x(), p.x()) * m_channel_count;

        // The sample could be out of bounds
        active = active && ek::all(p < m_size);

        // Accumulate!
        if constexpr (!JIT) {
            if (unlikely(!active))
                return;

            ScalarFloat *ptr = m_tensor.array().data() + index;
            for (uint32_t k = 0; k < m_channel_count; ++k)
                *ptr++ += values[k];
        } else {
            for (uint32_t k = 0; k < m_channel_count; ++k) {
                ek::scatter_reduce(ReduceOp::Add, m_tensor.array(), values[k],
                                   index, active);
                index++;
            }
        }

        return;
    }

    // ===================================================================
    // Prelude for the general case
    // ===================================================================

    ScalarFloat radius = m_rfilter->radius();

    // Size of the underlying image buffer
    ScalarVector2u size = m_size + 2 * m_border_size;

    // Check if the operation can be performed using a recorded loop
    bool record_loop = false;

    if constexpr (JIT) {
        record_loop = jit_flag(JitFlag::LoopRecord) && !m_normalize;

        if constexpr (ek::is_diff_array_v<Float>) {
            record_loop = record_loop &&
                          !ek::grad_enabled(pos) &&
                          !ek::grad_enabled(m_tensor);

            for (uint32_t k = 0; k < m_channel_count; ++k)
                record_loop = record_loop && !ek::grad_enabled(values[k]);
        }
    }

    // ===================================================================
    // 1. Non-coalesced accumulation method (see ImageBlock constructor)
    // ===================================================================

    if (!JIT || !m_coalesce) {
        Point2f pos_f   = pos + ((int) m_border_size - ScalarPoint2i(m_offset) - .5f),
                pos_0_f = pos_f - radius,
                pos_1_f = pos_f + radius;

        // Interval specifying the pixels covered by the filter
        Point2u pos_0_u = Point2u(ek::max(ek::ceil2int <Point2i>(pos_0_f), ScalarPoint2i(0))),
                pos_1_u = Point2u(ek::min(ek::floor2int<Point2i>(pos_1_f), ScalarPoint2i(size - 1))),
                count_u = pos_1_u - pos_0_u + 1u;

        // Base index of the top left corner
        UInt32 index =
            ek::fmadd(pos_0_u.y(), size.x(), pos_0_u.x()) * m_channel_count;

        // Compute the number of filter evaluations needed along each axis
        ScalarVector2u count;
        if constexpr (!JIT) {
            if (ek::any(pos_0_u > pos_1_u))
                return;
            count = count_u;
        } else {
            // Conservative bounds must be used in the vectorized case
            count = ek::ceil2int<uint32_t>(2.f * radius);
            active &= ek::all(pos_0_u <= pos_1_u);
        }

        Point2f rel_f = Point2f(pos_0_u) - pos_f;

        if (!record_loop) {
            // ===========================================================
            // 1.1. Scalar mode / unroll the complete loop
            // ===========================================================

            // Allocate memory for reconstruction filter weights on the stack
            Float *weights_x = (Float *) alloca(sizeof(Float) * count.x()),
                  *weights_y = (Float *) alloca(sizeof(Float) * count.y());

            // Evaluate filters weights along the X and Y axes

            for (uint32_t i = 0; i < count.x(); ++i) {
                new (weights_x + i)
                    Float(JIT ? m_rfilter->eval(rel_f.x())
                              : m_rfilter->eval_discretized(rel_f.x()));
                rel_f.x() += 1.f;
            }

            for (uint32_t i = 0; i < count.y(); ++i) {
                new (weights_y + i)
                    Float(JIT ? m_rfilter->eval(rel_f.y())
                              : m_rfilter->eval_discretized(rel_f.y()));
                rel_f.y() += 1.f;
            }

            // Normalize sample contribution if desired
            if (unlikely(m_normalize)) {
                Float wx = 0.f, wy = 0.f;

                for (uint32_t i = 0; i < count.x(); ++i)
                    wx += weights_x[i];

                for (uint32_t i = 0; i < count.y(); ++i)
                    wy += weights_y[i];

                Float factor = ek::detach(wx * wy);

                if constexpr (JIT) {
                    factor = ek::select(ek::neq(factor, 0.f), ek::rcp(factor), 0.f);
                } else {
                    if (unlikely(factor == 0))
                        return;
                    factor = ek::rcp(factor);
                }

                for (uint32_t i = 0; i < count.x(); ++i)
                    weights_x[i] *= factor;
            }

            ScalarFloat *ptr = nullptr;
            if constexpr (!JIT)
                ptr = m_tensor.array().data();

            // Accumulate!
            for (uint32_t y = 0; y < count.y(); ++y) {
                Mask active_1 = active && y < count_u.y();

                for (uint32_t x = 0; x < count.x(); ++x) {
                    Mask active_2 = active_1 && x < count_u.x();

                    for (uint32_t k = 0; k < m_channel_count; ++k) {
                        Float weight = weights_x[x] * weights_y[y];

                        if constexpr (!JIT) {
                            ENOKI_MARK_USED(active_2);
                            ptr[index] = ek::fmadd(values[k], weight, ptr[index]);
                        } else {
                            ek::scatter_reduce(ReduceOp::Add, m_tensor.array(),
                                               values[k] * weight, index, active_2);
                        }

                        index++;
                    }
                }

                index += (size.x() - count.x()) * m_channel_count;
            }

            // Destruct weight variables
            for (uint32_t i = 0; i < count.x(); ++i)
                weights_x[i].~Float();

            for (uint32_t i = 0; i < count.y(); ++i)
                weights_y[i].~Float();
        } else {
            // ===========================================================
            // 1.2. Recorded loop mode
            // ===========================================================

            UInt32 ys = 0;
            ek::Loop<Mask> loop_1("ImageBlock::put() [1]", ys, index);
            loop_1.set_uniform();

            while (loop_1(ys < count.y())) {
                Float weight_y = m_rfilter->eval(rel_f.y() + Float(ys));
                Mask active_1 = active && (pos_0_u.y() + ys <= pos_1_u.y());

                UInt32 xs = 0;
                ek::Loop<Mask> loop_2("ImageBlock::put() [2]", xs, index);
                loop_2.set_uniform();

                while (loop_2(xs < count.x())) {
                    Float weight_x = m_rfilter->eval(rel_f.x() + Float(xs)),
                          weight = weight_x * weight_y;

                    Mask active_2 = active_1 && (pos_0_u.x() + xs <= pos_1_u.x());
                    for (uint32_t k = 0; k < m_channel_count; ++k) {
                        ek::scatter_reduce(ReduceOp::Add, m_tensor.array(),
                                           values[k] * weight, index, active_2);
                        index++;
                    }

                    xs++;
                }

                ys++;
                index += (size.x() - count.x()) * m_channel_count;
            }
        }

        return;
    }

    // ===================================================================
    // 2. Coalesced accumulation method (see ImageBlock constructor)
    // ===================================================================

    if (JIT && m_coalesce) {
        // Number of pixels that may need to be visited on either side (-n..n)
        uint32_t n = ek::ceil2int<uint32_t>(radius - .5f);

        // Number of pixels to be visited along each dimension
        uint32_t count = 2 * n + 1;

        // Determine integer position of top left pixel within the filter footprint
        Point2i pos_i = ek::floor2int<Point2i>(pos) - int(n);

        // Account for pixel offset of the image block instance
        Point2i pos_i_local =
            pos_i + ((int) m_border_size - ScalarPoint2i(m_offset));

        // Switch over to unsigned integers, compute pixel index
        UInt32 x = UInt32(pos_i_local.x()),
               y = UInt32(pos_i_local.y()),
               index = ek::fmadd(y, size.x(), x) * m_channel_count;

        // Evaluate filters weights along the X and Y axes
        Point2f rel_f = Point2f(pos_i) + .5f - pos;

        if (!record_loop) {
            // ===========================================================
            // 2.1. Unroll the complete loop
            // ===========================================================

            // Allocate memory for reconstruction filter weights on the stack
            Float *weights_x = (Float *) alloca(sizeof(Float) * count),
                  *weights_y = (Float *) alloca(sizeof(Float) * count);

            for (uint32_t i = 0; i < count; ++i) {
                Float weight_x = m_rfilter->eval(rel_f.x()),
                      weight_y = m_rfilter->eval(rel_f.y());

                if (unlikely(m_normalize)) {
                    ek::masked(weight_x, x + i >= size.x()) = 0.f;
                    ek::masked(weight_y, y + i >= size.y()) = 0.f;
                }

                new (weights_x + i) Float(weight_x);
                new (weights_y + i) Float(weight_y);

                rel_f += 1;
            }

            // Normalize sample contribution if desired
            if (unlikely(m_normalize)) {
                Float wx = 0.f, wy = 0.f;

                for (uint32_t i = 0; i < count; ++i) {
                    wx += weights_x[i];
                    wy += weights_y[i];
                }

                Float factor = ek::detach(wx * wy);
                factor = ek::select(ek::neq(factor, 0.f), ek::rcp(factor), 0.f);

                for (uint32_t i = 0; i < count; ++i)
                    weights_x[i] *= factor;
            }

            // Accumulate!
            for (uint32_t ys = 0; ys < count; ++ys) {
                Mask active_1 = active && y < size.y();

                for (uint32_t xs = 0; xs < count; ++xs) {
                    Mask active_2 = active_1 && x < size.x();
                    Float weight = weights_y[ys] * weights_x[xs];

                    for (uint32_t k = 0; k < m_channel_count; ++k) {
                        ek::scatter_reduce(ReduceOp::Add, m_tensor.array(),
                                           values[k] * weight, index, active_2);
                        index++;
                    }

                    x++;
                }

                x -= count;
                y += 1;
                index += (size.x() - count) * m_channel_count;
            }

            // Destruct weight variables
            for (uint32_t i = 0; i < count; ++i) {
                weights_x[i].~Float();
                weights_y[i].~Float();
            }
        } else {
            // ===========================================================
            // 2.2. Recorded loop mode
            // ===========================================================

            UInt32 ys = 0;

            ek::Loop<Mask> loop_1("ImageBlock::put() [1]", ys, index);
            loop_1.set_uniform();

            while (loop_1(ys < count)) {
                Float weight_y = m_rfilter->eval(rel_f.y() + Float(ys));
                Mask active_1 = active && (y + ys < size.y());

                UInt32 xs = 0;
                ek::Loop<Mask> loop_2("ImageBlock::put() [2]", xs, index);
                loop_2.set_uniform();

                while (loop_2(xs < count)) {
                    Float weight_x = m_rfilter->eval(rel_f.x() + Float(xs)),
                          weight = weight_x * weight_y;

                    Mask active_2 = active_1 && (x + xs < size.x());
                    for (uint32_t k = 0; k < m_channel_count; ++k) {
                        ek::scatter_reduce(ReduceOp::Add, m_tensor.array(),
                                           values[k] * weight, index, active_2);
                        index++;
                    }

                    xs++;
                }

                ys++;
                index += (size.x() - count) * m_channel_count;
            }
        }
    }
}

MTS_VARIANT void ImageBlock<Float, Spectrum>::read(const Point2f &pos_,
                                                   Float *values,
                                                   Mask active) const {
    constexpr bool JIT = ek::is_jit_array_v<Float>;

    // Account for image block offset
    Point2f pos = pos_ - ScalarVector2f(m_offset);

    // ===================================================================
    //  Fast special case for the box filter
    // ===================================================================

    if (!m_rfilter) {
        Point2u p = Point2u(ek::floor2int<Point2i>(pos));

        // Switch over to unsigned integers, compute pixel index
        UInt32 index = ek::fmadd(p.y(), m_size.x(), p.x()) * m_channel_count;

        // The sample could be out of bounds
        active = active && ek::all(p < m_size);

        // Gather!
        for (uint32_t k = 0; k < m_channel_count; ++k) {
            values[k] = ek::gather<Float>(m_tensor.array(), index, active);
            index++;
        }

        return;
    }

    // ===================================================================
    // Prelude for the general case
    // ===================================================================

    ScalarFloat radius = m_rfilter->radius();

    // Size of the underlying image buffer
    ScalarVector2u size = m_size + 2 * m_border_size;

    // Check if the operation can be performed using a recorded loop
    bool record_loop = false;

    if constexpr (JIT) {
        record_loop = jit_flag(JitFlag::LoopRecord);

        if constexpr (ek::is_diff_array_v<Float>) {
            record_loop = record_loop &&
                          !ek::grad_enabled(pos) &&
                          !ek::grad_enabled(m_tensor);

            for (uint32_t k = 0; k < m_channel_count; ++k)
                record_loop = record_loop && !ek::grad_enabled(values[k]);
        }
    }

    // Exclude areas that are outside of the block
    active &= ek::all(pos >= 0.f) && ek::all(pos < m_size);

    // Zero-initialize output array
    for (uint32_t i = 0; i < m_channel_count; ++i)
        values[i] = ek::zero<Float>(ek::width(pos));

    Point2f pos_f   = pos + ((int) m_border_size - .5f),
            pos_0_f = pos_f - radius,
            pos_1_f = pos_f + radius;

    // Interval specifying the pixels covered by the filter
    Point2u pos_0_u = Point2u(ek::max(ek::ceil2int <Point2i>(pos_0_f), ScalarPoint2i(0))),
            pos_1_u = Point2u(ek::min(ek::floor2int<Point2i>(pos_1_f), ScalarPoint2i(size - 1))),
            count_u = pos_1_u - pos_0_u + 1u;

    // Base index of the top left corner
    UInt32 index =
        ek::fmadd(pos_0_u.y(), size.x(), pos_0_u.x()) * m_channel_count;

    // Compute the number of filter evaluations needed along each axis
    ScalarVector2u count;
    if constexpr (!JIT) {
        if (ek::any(pos_0_u > pos_1_u))
            return;
        count = count_u;
    } else {
        // Conservative bounds must be used in the vectorized case
        count = ek::ceil2int<uint32_t>(2.f * radius);
        active &= ek::all(pos_0_u <= pos_1_u);
    }

    Point2f rel_f = Point2f(pos_0_u) - pos_f;

    if (!record_loop) {
        // ===========================================================
        // 1.1. Scalar mode / unroll the complete loop
        // ===========================================================

        // Allocate memory for reconstruction filter weights on the stack
        Float *weights_x = (Float *) alloca(sizeof(Float) * count.x()),
              *weights_y = (Float *) alloca(sizeof(Float) * count.y());

        // Evaluate filters weights along the X and Y axes

        for (uint32_t i = 0; i < count.x(); ++i) {
            new (weights_x + i)
                Float(JIT ? m_rfilter->eval(rel_f.x())
                          : m_rfilter->eval_discretized(rel_f.x()));
            rel_f.x() += 1.f;
        }

        for (uint32_t i = 0; i < count.y(); ++i) {
            new (weights_y + i)
                Float(JIT ? m_rfilter->eval(rel_f.y())
                          : m_rfilter->eval_discretized(rel_f.y()));
            rel_f.y() += 1.f;
        }

        // Normalize sample contribution if desired
        if (unlikely(m_normalize)) {
            Float wx = 0.f, wy = 0.f;

            for (uint32_t i = 0; i < count.x(); ++i)
                wx += weights_x[i];

            for (uint32_t i = 0; i < count.y(); ++i)
                wy += weights_y[i];

            Float factor = ek::detach(wx * wy);

            if constexpr (JIT) {
                factor = ek::select(ek::neq(factor, 0.f), ek::rcp(factor), 0.f);
            } else {
                if (unlikely(factor == 0))
                    return;
                factor = ek::rcp(factor);
            }

            for (uint32_t i = 0; i < count.x(); ++i)
                weights_x[i] *= factor;
        }

        // Gather!
        for (uint32_t y = 0; y < count.y(); ++y) {
            Mask active_1 = active && y < count_u.y();

            for (uint32_t x = 0; x < count.x(); ++x) {
                Mask active_2 = active_1 && x < count_u.x();

                Float weight = weights_x[x] * weights_y[y];

                for (uint32_t k = 0; k < m_channel_count; ++k) {
                    values[k] = ek::fmadd(
                        ek::gather<Float>(m_tensor.array(), index, active_2),
                        weight, values[k]);

                    index++;
                }
            }

            index += (size.x() - count.x()) * m_channel_count;
        }

        // Destruct weight variables
        for (uint32_t i = 0; i < count.x(); ++i)
            weights_x[i].~Float();

        for (uint32_t i = 0; i < count.y(); ++i)
            weights_y[i].~Float();
    } else {
        // ===========================================================
        // 1.2. Recorded loop mode
        // ===========================================================

        UInt32 ys = 0;
        Float weight_sum = 0.f;

        ek::Loop<Mask> loop_1("ImageBlock::read() [1]");
        loop_1.set_uniform();
        loop_1.put(ys, index, weight_sum);
        for (uint32_t k = 0; k < m_channel_count; ++k)
            loop_1.put(values[k]);
        loop_1.init();

        while (loop_1(ys < count.y())) {
            Float weight_y = m_rfilter->eval(rel_f.y() + Float(ys));
            Mask active_1 = active && (pos_0_u.y() + ys <= pos_1_u.y());

            UInt32 xs = 0;
            ek::Loop<Mask> loop_2("ImageBlock::read() [2]");

            loop_2.set_uniform();
            loop_2.put(xs, index, weight_sum);
            for (uint32_t k = 0; k < m_channel_count; ++k)
                loop_2.put(values[k]);
            loop_2.init();

            while (loop_2(xs < count.x())) {
                Float weight_x = m_rfilter->eval(rel_f.x() + Float(xs)),
                      weight = weight_x * weight_y;

                Mask active_2 = active_1 && (pos_0_u.x() + xs <= pos_1_u.x());
                for (uint32_t k = 0; k < m_channel_count; ++k) {
                    values[k] = ek::fmadd(
                        ek::gather<Float>(m_tensor.array(), index, active_2),
                        weight, values[k]);

                    index++;
                }

                weight_sum += ek::select(active_2, weight, 0.f);
                xs++;
            }

            ys++;
            index += (size.x() - count.x()) * m_channel_count;
        }

        if (m_normalize) {
            Float norm =
                ek::select(ek::neq(weight_sum, 0.f), ek::rcp(weight_sum), 0.f);

            for (uint32_t k = 0; k < m_channel_count; ++k)
                values[k] *= norm;
        }
    }
}

MTS_VARIANT std::string ImageBlock<Float, Spectrum>::to_string() const {
    std::ostringstream oss;

    oss << "ImageBlock[" << std::endl
        << "  offset = " << m_offset << "," << std::endl
        << "  size = " << m_size << "," << std::endl
        << "  border_size = " << m_border_size << "," << std::endl
        << "  normalize = " << m_normalize << "," << std::endl
        << "  coalesce = " << m_coalesce << "," << std::endl
        << "  warn_negative = " << m_warn_negative << "," << std::endl
        << "  warn_invalid = " << m_warn_invalid << "," << std::endl
        << "  rfilter = " << (m_rfilter ? string::indent(m_rfilter) : "nullptr")
        << std::endl
        << "]";

    return oss.str();
}

MTS_IMPLEMENT_CLASS_VARIANT(ImageBlock, Object)
MTS_INSTANTIATE_CLASS(ImageBlock)
NAMESPACE_END(mitsuba)
