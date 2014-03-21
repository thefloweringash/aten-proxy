# -*- mode: ruby -*-
# vi: set ft=ruby :

VAGRANTFILE_API_VERSION = "2"

Vagrant.configure(VAGRANTFILE_API_VERSION) do |config|
  config.vm.box = "ubuntu-saucy-amd64-20140226"
  config.vm.box_url = "http://cloud-images.ubuntu.com/vagrant/saucy/20140226/saucy-server-cloudimg-amd64-vagrant-disk1.box"
  config.vm.provision :puppet do |puppet|
    puppet.manifests_path = "vagrant"
    puppet.manifest_file  = "site.pp"
  end
end
