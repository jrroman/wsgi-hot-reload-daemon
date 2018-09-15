# WSGI hot reload
FYI dockerfile is for testing... currently running on OSX aka inotify is not local 

Currently the daemon init script is only configured for sysvinit... `/etc/init.d`. However if an init script is written for something else everything should work normally

**after install:**

*logs located at*
`/var/log/daemon.log`

*executable located at*
`/usr/bin/daemon`

*init script located at*
`/etc/init.d/daemon`

### Installation

```sh
# clone repository into env
git clone git@github.com:jrroman/wsgi-hot-reload-daemon.git

# ensure make and gcc are in the env
# debian / ubuntu
apt-get update && apt-get install -y make gcc

# add the daemon init script
cp daemon.init /etc/init.d/daemon \
&& chmod +x /etc/init.d/daemon \
&& update-rc.d daemon defaults

# compile the daemon
# this will place the exec in /usr/bin
./build.sh

# run the daemon
service daemon start
**
