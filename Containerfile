FROM archlinux:latest AS base

WORKDIR /br

ENV DEVKITPRO=/opt/devkitpro
ENV DEVKITARM=/opt/devkitpro/devkitARM
ENV DEVKITPPC=/opt/devkitpro/devkitPPC

RUN pacman --noconfirm -Sy wget make zip git

RUN pacman-key --init
RUN pacman-key --recv BC26F752D25B92CE272E0F44F7FD5492264BB9D0 --keyserver keyserver.ubuntu.com
RUN pacman-key --lsign BC26F752D25B92CE272E0F44F7FD5492264BB9D0

RUN wget https://pkg.devkitpro.org/devkitpro-keyring.pkg.tar.xz
RUN pacman --noconfirm -U devkitpro-keyring.pkg.tar.xz

RUN printf '\n[dkp-libs]\nServer = https://pkg.devkitpro.org/packages' >> /etc/pacman.conf
RUN printf '\n[dkp-linux]\nServer = https://pkg.devkitpro.org/packages/linux/$arch/' >> /etc/pacman.conf

RUN pacman --noconfirm -Syu switch-dev switch-glm switch-libjpeg-turbo

COPY . .

RUN cd lib/libnx && make -j $(nproc)
RUN cd lib/libnx && make install

RUN cd lib/Atmosphere-libs/libstratosphere && make -j $(nproc)

RUN make dist -j$(nproc)
