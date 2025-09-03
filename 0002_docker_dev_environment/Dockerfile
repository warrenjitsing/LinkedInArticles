# Article is the docs :)
FROM debian:12
LABEL authors="warren_jitsing"

ARG py311="3.11.13"
ARG py312="3.12.11"
ARG py313="3.13.7"
ARG gcc15="15.2.0"
ARG USERNAME
ARG USER_UID
ARG USER_GID
ARG SSH_DIR

RUN apt update \
    && apt install -y \
        sudo openssh-client openssh-server nano procps less build-essential cmake curl wget git \
        pkg-config zlib1g-dev libncurses5-dev libgdbm-dev libnss3-dev libssl-dev libreadline-dev \
        libffi-dev libsqlite3-dev wget curl pkg-config libbz2-dev libgdbm-compat-dev liblzma-dev \
        lzma uuid-dev python3-tk ca-certificates openssl ca-certificates fontconfig git-lfs \
        patch tzdata netbase iproute2 python3-full python3-dev python3-pip wget libzstd-dev \
        libzstd1 flex libfl-dev libfl2 m4 gnupg2 tmux \
    && apt upgrade -y \
    && apt-get autoremove -y \
    && rm -rf /var/lib/apt/lists/* \
    && apt-get purge -y --auto-remove -o APT::AutoRemove::RecommendsImportant=false

RUN groupadd --gid $USER_GID $USERNAME \
    && useradd --uid $USER_UID --gid $USER_GID -m $USERNAME \
    && sed -i "s/#PubkeyAuthentication yes/PubkeyAuthentication yes/g" /etc/ssh/sshd_config \
    && echo $USERNAME ALL=\(root\) NOPASSWD:ALL > /etc/sudoers.d/$USERNAME \
    && chmod 0440 /etc/sudoers.d/$USERNAME \
    && echo 'export GPG_TTY=$(tty)' >> /home/$USERNAME/.bashrc

RUN mkdir ~/deps && \
    cd ~/deps && \
    git clone --depth=1 -b releases/gcc-$gcc15 https://github.com/gcc-mirror/gcc.git && \
    cd gcc && \
    ./contrib/download_prerequisites && \
    mkdir build && cd build && \
    ../configure --disable-multilib --enable-languages=c,c++ && make -j 12 && make install  && \
    cd && rm -rf deps/gcc && \
    echo "export CC=/usr/local/bin/gcc" >> ~/.bashrc  && \
    echo "export CXX=/usr/local/bin/g++" >> ~/.bashrc  && \
    echo "export CC=/usr/local/bin/gcc" >> /home/$USERNAME/.bashrc  && \
    echo "export CXX=/usr/local/bin/g++" >> /home/$USERNAME/.bashrc


RUN for version in $py311 $py312 $py313; do \
        echo "--- Building Python version ${version} ---"; \
        wget https://github.com/python/cpython/archive/refs/tags/v${version}.tar.gz; \
        tar -xzf v${version}.tar.gz; \
        cd cpython-${version}; \
        \
        CONFIGURE_FLAGS="--enable-optimizations --enable-loadable-sqlite-extensions --with-lto=full"; \
        if [ "$version" = "$py313" ]; then \
            echo "--- Adding --disable-gil flag for nogil build ---"; \
            CONFIGURE_FLAGS="$CONFIGURE_FLAGS --disable-gil"; \
        fi; \
        \
        ./configure $CONFIGURE_FLAGS; \
        make -j 12; \
        sudo make altinstall; \
        cd ..; \
    done && \
    rm -rf v*.tar.gz && \
    rm -rf cpython-*

USER $USERNAME
WORKDIR /home/$USERNAME

RUN curl https://sh.rustup.rs -sSf | sh -s -- -y


RUN python3.12 -m venv /home/$USERNAME/.venv_jupyter && \
    . /home/$USERNAME/.venv_jupyter/bin/activate && \
    python3 -m pip install --no-cache-dir jupyterlab

COPY --chown=$USERNAME:$USERNAME --chmod=700 $SSH_DIR /home/$USERNAME/.ssh

RUN cat /home/$USERNAME/.ssh/id_rsa.pub > /home/$USERNAME/.ssh/authorized_keys

RUN printf "Host github\n\
    HostName github.com\n\
    IdentityFile ~/.ssh/id_rsa\n\
    StrictHostKeyChecking no\n\
\n\
Host your-private-gitlab\n\
    HostName git.example.com\n\
    Port 2222\n\
    IdentityFile ~/.ssh/id_rsa\n\
    StrictHostKeyChecking no\n\
" > /home/$USERNAME/.ssh/config

COPY --chown=$USERNAME:$USERNAME --chmod=755 ./entrypoint.sh /entrypoint.sh

EXPOSE 22 8888
ENTRYPOINT ["/entrypoint.sh"]