#
# https://github.com/microsoft/vscode-dev-containers/tree/main/containers/debian
# https://github.com/pgit/cpp-devcontainer
#
FROM docker.io/psedoc/cpp-devcontainer:0.17 AS ci-base

#
# Install 'ping' used by Process.* tests
#
RUN apt-get update && export DEBIAN_FRONTEND=noninteractive && \
    apt-get -y install iputils-ping tmux btop && \
    apt-get clean && \
    rm -rf /var/lib/apt/lists/*

# --------------------------------------------------------------------------------------------------

#
# Target 'ci-base' is used in automated GitHub workflows. It contains only the packages needed to
# run the tests, but none of the extra steps below for a convenient interactive devcontainer.
#
FROM ci-base

#
# GDB libc++ pretty printer (works for string, but not for map with llvm-17)
#
ARG USERNAME=vscode
RUN cd /home/${USERNAME} && \
    git clone https://github.com/koutheir/libcxx-pretty-printers.git && \
    chown -R $USERNAME libcxx-pretty-printers && \
    echo >>.gdbinit "python\nimport sys\nsys.path.insert(0, '/home/${USERNAME}/libcxx-pretty-printers/src')\nfrom libcxx.v1.printers import register_libcxx_printers\nregister_libcxx_printers(None)\nend" && \
    chown $USERNAME .gdbinit

#
# ASIO Debugger Extensions
#
# https://github.com/chriskohlhoff/asio-debugger-extensions.git
#
ARG USERNAME=vscode
RUN cd /home/${USERNAME} && \
    git clone https://github.com/chriskohlhoff/asio-debugger-extensions.git && \
    chown -R $USERNAME asio-debugger-extensions && \
    echo >>.gdbinit "source /home/${USERNAME}/asio-debugger-extensions/gdb/awaitable_backtrace.py" && \
    echo >>.gdbinit "source /home/${USERNAME}/asio-debugger-extensions/gdb/pretty_printer.py" && \
    chown $USERNAME .gdbinit

#
# Customize Environment
#
RUN echo "export LESS=RS" >> "/home/$USERNAME/.bashrc"
RUN echo "alias l='ls -la'" >> "/home/$USERNAME/.bashrc"

#
# enable persistent bash history in container
#
# https://code.visualstudio.com/remote/advancedcontainers/persist-bash-history
#
# To actually make it persistent across re-creation of the container, you need to
# mount a volume to /commandhistory. You can do this in devcontainer.json like this:
#
# 	"mounts": [
# 		"source=cpp-devcontainer-bashhistory,target=/commandhistory,type=volume"
#   ]
#
ARG USERNAME=vscode
RUN SNIPPET="export PROMPT_COMMAND='history -a' && export HISTFILE=/commandhistory/.bash_history" \
    && mkdir /commandhistory \
    && touch /commandhistory/.bash_history \
    && chown -R $USERNAME /commandhistory \
    && echo "$SNIPPET" >> "/home/$USERNAME/.bashrc"

#
# Copy .tmux.conf and .bash_aliases from .devcontainer to user's home
#
ARG USERNAME=vscode
COPY .tmux.conf /home/${USERNAME}/.tmux.conf
COPY .bash_aliases /home/${USERNAME}/.bash_aliases
RUN chown $USERNAME:$USERNAME /home/${USERNAME}/.tmux.conf /home/${USERNAME}/.bash_aliases

# --------------------------------------------------------------------------------------------------

SHELL ["/bin/bash", "-c"]
