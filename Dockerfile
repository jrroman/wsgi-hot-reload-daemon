FROM ubuntu:trusty

WORKDIR /daemon
COPY . .

RUN apt-get update && apt-get install -y \
    build-essential \
    gcc \
    make

CMD ["./build.sh"]
