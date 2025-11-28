#
# https://github.com/microsoft/vscode-dev-containers/tree/main/containers/debian
# https://github.com/pgit/cpp-devcontainer
#
FROM docker.io/psedoc/cpp-devcontainer:0.21 AS ci-base

#
# Install 'ping' used by Process.* tests
#
RUN apt-get update && export DEBIAN_FRONTEND=noninteractive && \
    apt-get -y install iputils-ping tmux btop nghttp2 && \
    apt-get clean && \
    rm -rf /var/lib/apt/lists/*

ARG VERSION=1.4.3
RUN wget https://github.com/danielaparker/jsoncons/archive/refs/tags/v${VERSION}.tar.gz && \
    tar xzvf v${VERSION}.tar.gz && \
    mv jsoncons-${VERSION}/include/* /usr/local/include && \
    rm jsoncons-${VERSION} v${VERSION}.tar.gz -rf

RUN git clone https://github.com/Tencent/rapidjson.git && \ 
    mv rapidjson/include/* /usr/local/include && \
    rm rapidjson -rf
    
# --------------------------------------------------------------------------------------------------

#
# Target 'ci-base' is used in automated GitHub workflows. It contains only the packages needed to
# run the tests, but none of the extra steps below for a convenient interactive devcontainer.
#
FROM ci-base

#
# https://clang.llvm.org/docs/DebuggingCoroutines.html#lldb-debugger-script
#
# Adds "coro bt" to LLDB, but doesn't seem to work with ASIO frames.
#
ARG USERNAME=vscode
COPY lldb_coro_debugging.py /usr/local/lib/lldb/lldb_coro_debugging.py
RUN cd /home/${USERNAME} && \
    echo >>.lldbinit "command script import /usr/local/lib/lldb/lldb_coro_debugging.py" && \
    chown ${USERNAME} .lldbinit

#
# ASIO Debugger Extensions
#
# FIXME: don't seem to work, search for something that does
#
# https://github.com/chriskohlhoff/asio-debugger-extensions.git
#
# ARG USERNAME=vscode
# RUN cd /home/${USERNAME} && \
#     git clone https://github.com/chriskohlhoff/asio-debugger-extensions.git && \
#     chown -R $USERNAME asio-debugger-extensions && \
#     echo >>.gdbinit "source /home/${USERNAME}/asio-debugger-extensions/gdb/awaitable_backtrace.py" && \
#     echo >>.gdbinit "source /home/${USERNAME}/asio-debugger-extensions/gdb/pretty_printer.py" && \
#     chown $USERNAME .gdbinit

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
