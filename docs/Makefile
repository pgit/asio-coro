# Minimal makefile for Sphinx documentation

# You can set these variables from the command line, and also
# from the environment for the first two.
SPHINXOPTS    ?=
SPHINXBUILD   ?= ../.venv/bin/sphinx-build
SOURCEDIR     = .
BUILDDIR      = _build
RST_SOURCE    = $(SOURCEDIR)/process_cancellation_from_tests.rst

.PHONY: rst
rst: $(RST_SOURCE)

$(RST_SOURCE): ../scripts/generate_process_cancellation_rst.py ../test/test_process_cancellation.cpp
	@echo "[RST] Generating $@"
	@cd .. && scripts/generate_process_cancellation_rst.py --output docs/process_cancellation_from_tests.rst

../scripts/generate_process_cancellation_rst.py:
	@:

# Put it first so that "make" without argument is like "make help".
help:
	@$(SPHINXBUILD) -M help "$(SOURCEDIR)" "$(BUILDDIR)" $(SPHINXOPTS) $(O)

.PHONY: help Makefile

# Catch-all target: route all unknown targets to Sphinx using the new
# "make mode" option.  $(O) is meant as a shortcut for $(SPHINXOPTS).
%: Makefile rst
	@$(SPHINXBUILD) -M $@ "$(SOURCEDIR)" "$(BUILDDIR)" $(SPHINXOPTS) $(O)
