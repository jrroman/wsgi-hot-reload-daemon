FROM ubuntu:trusty

WORKDIR /daemon
COPY . .

RUN apt-get update && apt-get install -y \
    build-essential \
    gcc \
    make \
    && cp daemon.init /etc/init.d/daemon \
    && chmod +x /etc/init.d/daemon \
    && update-rc.d daemon defaults \
    && ./build.sh && mv daemon /usr/bin

CMD ["service daemon start"]
