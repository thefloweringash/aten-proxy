node default {
  exec {'apt-get update':
	path=>['/bin', '/sbin', '/usr/bin', '/usr/sbin']
  }
  Exec['apt-get update'] -> Package<| |>

  package{['build-essential', 'pkg-config',
		   'libev4', 'libev-dev',
		   'libvncserver0', 'libvncserver-dev']:
	ensure => latest
  }
}
