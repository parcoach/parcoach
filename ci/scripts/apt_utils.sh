apt_install() {
  apt-get update

  apt-get install -yqq --no-install-recommends "$@"
}

cleanup() {
  apt-get autoremove -yqq

  # Remove doc and stuff...
  rm -rf /var/lib/{cache,log} /usr/share/doc
}
