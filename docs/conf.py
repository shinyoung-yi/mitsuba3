#!/usr/bin/env python3
# -*- coding: utf-8 -*-
#
# Mitsuba 3 documentation build configuration file
#
# The documentation can be built by invoking "make mkdoc"
# from the build directory

import sys
import os

from sphinx.writers.html5 import HTML5Translator

# Work around an odd exception on readthedocs.org
vr = HTML5Translator.visit_reference
def replacement(self, node):
    if 'refuri' not in node and 'refid' not in node:
        print(node)
        return
    vr(self, node)
HTML5Translator.visit_reference = replacement

if not os.path.exists('src/tutorials'):
    os.symlink('../../tutorials', 'src/tutorials', target_is_directory=True)

if not os.path.exists('src/getting_started/tutorials/'):
    os.symlink('../../tutorials/getting_started', 'src/getting_started/tutorials', target_is_directory=True)

if not os.path.exists('src/generated'):
    os.symlink('../generated', 'src/generated', target_is_directory=True)

# This is necessary for the plugin doc to properly access the resources (images)
if not os.path.exists('resources'):
    os.symlink('../resources', 'resources', target_is_directory=True)

from pathlib import Path

# If extensions (or modules to document with autodoc) are in another directory,
# add these directories to sys.path here. If the directory is relative to the
# documentation root, use os.path.abspath to make it absolute, like shown here.
#sys.path.insert(0, os.path.abspath('.'))

# -- General configuration ------------------------------------------------

# If your documentation needs a minimal Sphinx version, state it here.
needs_sphinx = '2.4'


# Add any paths that contain templates here, relative to this directory.
templates_path = ['_templates']

# The suffix(es) of source filenames.
# You can specify multiple suffix as a list of string:
# source_suffix = ['.rst', '.md']
source_suffix = '.rst'

rst_prolog = r"""
.. role:: paramtype

.. role:: monosp

.. |spectrum| replace:: :paramtype:`spectrum`
.. |texture| replace:: :paramtype:`texture`
.. |float| replace:: :paramtype:`float`
.. |bool| replace:: :paramtype:`boolean`
.. |int| replace:: :paramtype:`integer`
.. |false| replace:: :monosp:`false`
.. |true| replace:: :monosp:`true`
.. |string| replace:: :paramtype:`string`
.. |bsdf| replace:: :paramtype:`bsdf`
.. |phase| replace:: :paramtype:`phase`
.. |point| replace:: :paramtype:`point`
.. |vector| replace:: :paramtype:`vector`
.. |transform| replace:: :paramtype:`transform`
.. |volume| replace:: :paramtype:`volume`
.. |tensor| replace:: :paramtype:`tensor`

.. |drjit| replace:: :monosp:`drjit`
.. |numpy| replace:: :monosp:`numpy`

.. |nbsp| unicode:: 0xA0
   :trim:

.. |exposed| replace:: :abbr:`P (This parameters will be exposed as a scene parameter)`
.. |differentiable| replace:: :abbr:`∂ (This parameter is differentiable)`
.. |discontinuous| replace:: :abbr:`D (This parameter might introduce discontinuities. Therefore it requires special handling during differentiation to prevent bias (e.g. prb-reparam)))`

"""


# The encoding of source files.
#source_encoding = 'utf-8-sig'

# The master toctree document.
master_doc = 'index'

# General information about the project.
project = 'mitsuba3'
copyright = '2022, Realistic Graphics Lab (RGL), EPFL'
author = 'Realistic Graphics Lab, EPFL'

# The version info for the project you're documenting, acts as replacement for
# |version| and |release|, also used in various other places throughout the
# built documents.
#
# The short X.Y version.
version = '3.0'
# The full version, including alpha/beta/rc tags.
release = '3.0.0'

# The language for content autogenerated by Sphinx. Refer to documentation
# for a list of supported languages.
#
# This is also used if you do content translation via gettext catalogs.
# Usually you set "language" from the command line for these cases.
language = None

# There are two options for replacing |today|: either, you set today to some
# non-false value, then it is used:
#today = ''
# Else, today_fmt is used as the format for a strftime call.
#today_fmt = '%B %d, %Y'

# List of patterns, relative to source directory, that match files and
# directories to ignore when looking for source files.
exclude_patterns = ['.build',
                    'release.rst',
                    'src/plugin_reference/section_*.rst',
                    'docs_api/*',
                    'generated/extracted_rst_api.rst',
                    '**.ipynb_checkpoints']

# The reST default role (used for this markup: `text`) to use for all
# documents.
default_role = 'any'

# If true, '()' will be appended to :func: etc. cross-reference text.
#add_function_parentheses = True

# If true, the current module name will be prepended to all description
# unit titles (such as .. function::).
#add_module_names = True

# If true, sectionauthor and moduleauthor directives will be shown in the
# output. They are ignored by default.
#show_authors = False

# The name of the Pygments (syntax highlighting) style to use.
# pygments_style = 'colorful'
# pygments_style = 'tango'
# pygments_style = 'lovelace'

# A list of ignored prefixes for module index sorting.
#modindex_common_prefix = []

# If true, keep warnings as "system message" paragraphs in the built documents.
#keep_warnings = False

# If true, `todo` and `todoList` produce output, else they produce nothing.
todo_include_todos = False


# -- Options for HTML output ----------------------------------------------

# The theme to use for HTML and HTML Help pages.  See the documentation for
# a list of builtin themes.

html_logo = "images/logo.png"
html_title = "Mitsuba 3"
html_theme = 'furo'
html_static_path = ['_static']
html_js_files = ['override-theme.js']

# Register the theme as an extension to generate a sitemap.xml
extensions = []
extensions.append("sphinx.ext.mathjax")
extensions.append("sphinx_tabs.tabs")
extensions.append("hoverxref.extension")

sys.path.append(os.path.abspath('exts/sphinxtr'))
extensions.append('subfig')
extensions.append('figtable')
extensions.append('numfig')
extensions.append('pluginparameters')

extensions.append('sphinx.ext.todo')
todo_include_todos = True

extensions.append('sphinxcontrib.bibtex')

extensions.append('sphinx_panels')

extensions.append('nbsphinx')
nbsphinx_execute = 'never'

# nbsphinx_input_prompt = 'In [%s]:'
# nbsphinx_output_prompt = 'Out [%s]:'
nbsphinx_prolog = """
.. raw:: html

    <style>

    </style>

    <div id="nb_link" class="admonition topic alert alert-block alert-success">
        <a href="#">
            <center>📑⬇️ Download Jupyter notebook</center>
        </a>
    </div>

    <script>
        var path = window.location.pathname;
        var pos = path.lastIndexOf("/tutorials/");
        var name = path.slice(pos + 11, -5);
        var tuto_url = "https://github.com/mitsuba-renderer/mitsuba-tutorials/blob/master/";
        var elem = document.getElementById("nb_link").firstElementChild;
        elem.href = tuto_url + name + ".ipynb";
    </script>
"""

extensions.append('sphinx_gallery.load_style')
nbsphinx_thumbnails = {
    'src/tutorials/getting_started/quickstart/drjit_quickstart': '_static/drjit-logo-dark.png',
}

extensions.append('sphinx_copybutton')

# Add bibfile
bibtex_bibfiles = ['references.bib']

# Touch the bibliography file to force a rebuild of it
Path('zz_bibliography.rst').touch()

# Theme options are theme-specific and customize the look and feel of a theme
# further.  For a list of options available for each theme, see the
# documentation.
#html_theme_options = {}

# Add any paths that contain custom themes here, relative to this directory.
#html_theme_path = []

# The name for this set of Sphinx documents.  If None, it defaults to
# "<project> v<release> documentation".

#html_title = None

# A shorter title for the navigation bar.  Default is the same as html_title.
#html_short_title = None

# The name of an image file (relative to this directory) to place at the top
# of the sidebar.
# html_logo = None

# The name of an image file (within the static path) to use as favicon of the
# docs.  This file should be a Windows icon file (.ico) being 16x16 or 32x32
# pixels large.
#html_favicon = None

# Add any paths that contain custom static files (such as style sheets) here,
# relative to this directory. They are copied after the builtin static files,
# so a file named "default.css" will overwrite the builtin "default.css".
html_static_path = ['_static']

# Add any extra paths that contain custom files (such as robots.txt or
# .htaccess) here, relative to this directory. These files are copied
# directly to the root of the documentation.
#html_extra_path = []

# If not '', a 'Last updated on:' timestamp is inserted at every page bottom,
# using the given strftime format.
#html_last_updated_fmt = '%b %d, %Y'

# Custom sidebar templates, maps document names to template names.
#html_sidebars = {}

# Additional templates that should be rendered to pages, maps page names to
# template names.
#html_additional_pages = {}

# If false, no module index is generated.
#html_domain_indices = True

# If false, no index is generated.
#html_use_index = True

# If true, the index is split into individual pages for each letter.
#html_split_index = False

# If true, links to the reST sources are added to the pages.
html_show_sourcelink = False

# If true, "Created using Sphinx" is shown in the HTML footer. Default is True.
#html_show_sphinx = True

# If true, "(C) Copyright ..." is shown in the HTML footer. Default is True.
#html_show_copyright = True

# If true, an OpenSearch description file will be output, and all pages will
# contain a <link> tag referring to it.  The value of this option must be the
# base URL from which the finished HTML is served.
#html_use_opensearch = ''

# This is the file name suffix for HTML files (e.g. ".xhtml").
#html_file_suffix = None

# Language to be used for generating the HTML full-text search index.
# Sphinx supports the following languages:
#   'da', 'de', 'en', 'es', 'fi', 'fr', 'h', 'it', 'ja'
#   'nl', 'no', 'pt', 'ro', 'r', 'sv', 'tr'
#html_search_language = 'en'

# A dictionary with options for the search language support, empty by default.
# Now only 'ja' uses this config value
#html_search_options = {'type': 'default'}

# The name of a javascript file (relative to the configuration directory) that
# implements a search results scorer. If empty, the default will be used.
#html_search_scorer = 'scorer.js'

# Output file base name for HTML help builder.
htmlhelp_basename = 'mitsuba3_doc'

# -- Options for LaTeX output ---------------------------------------------

latex_elements = {
    # The paper size ('letterpaper' or 'a4paper').
    #'papersize': 'letterpaper',

    # The font size ('10pt', '11pt' or '12pt').
    #'pointsize': '10pt',

    # Additional stuff for the LaTeX preamble.
    'preamble': '\DeclareUnicodeCharacter{00A0}{}',

    # Latex figure (float) alignment
    #'figure_align': 'htbp',
}

# Grouping the document tree into LaTeX files. List of tuples
# (source start file, target name, title,
#  author, documentclass [howto, manual, or own class]).
latex_documents = [
    (master_doc, 'mitsuba3.tex', 'Mitsuba 3 Documentation',
     'Wenzel Jakob', 'manual'),
]

# The name of an image file (relative to this directory) to place at the top of
# the title page.
# latex_logo = 'mitsuba-logo.png'

# For "manual" documents, if this is true, then toplevel headings are parts,
# not chapters.
#latex_use_parts = False

# If true, show page references after internal links.
#latex_show_pagerefs = False

# If true, show URL addresses after external links.
#latex_show_urls = False

# Documents to append as an appendix to all manuals.
#latex_appendices = []

# If false, no module index is generated.
#latex_domain_indices = True


# -- Options for manual page output ---------------------------------------

# One entry per manual page. List of tuples
# (source start file, name, description, authors, manual section).
man_pages = [
    (master_doc, 'mitsuba3', 'Mitsuba 3 Documentation',
     [author], 1)
]

# If true, show URL addresses after external links.
#man_show_urls = False


# -- Options for Texinfo output -------------------------------------------

# Grouping the document tree into Texinfo files. List of tuples
# (source start file, target name, title, author,
#  dir menu entry, description, category)
texinfo_documents = [
    (master_doc, 'mitsuba3', 'Mitsuba 3 Documentation',
     author, 'mitsuba3', 'One line description of project.',
     'Miscellaneous'),
]

# Documents to append as an appendix to all manuals.
#texinfo_appendices = []

# If false, no module index is generated.
#texinfo_domain_indices = True

# How to display URL addresses: 'footnote', 'no', or 'inline'.
#texinfo_show_urls = 'footnote'

# If true, do not generate a @detailmenu in the "Top" node's menu.
#texinfo_no_detailmenu = False

primary_domain = 'cpp'
highlight_language = 'cpp'

build_dir = os.path.join(os.path.dirname(
    os.path.abspath(__file__)), 'generated')


def custom_step(app):
    sys.path.append(os.path.dirname(os.path.abspath(__file__)))
    import generate_plugin_doc

    if not os.path.exists(build_dir):
        os.mkdir(build_dir)
    generate_plugin_doc.generate(build_dir)


# -- Register event callbacks ----------------------------------------------


def setup(app):
    # Texinfo
    app.connect("builder-inited", custom_step)
    app.add_css_file('theme_overrides.css')
