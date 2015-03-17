
```
git clone https://code.google.com/p/kbus/
```
  1. Build the kernel module. You will need to have the `kernel-headers` package for your current running kernel installed.
```
cd kbus
make -C kbus
```
  1. Ensure that the `kbus` device nodes get reasonable permissions.
    * If your system runs `udev` (most Debian and Ubuntu do) you can do this by installing the generated udev rules file.
```
sudo cp kbus/45-kbus.rules /etc/udev/rules.d/
```
    * or
```
cd kbus
sudo make rules
cd ..
```
    * On non-udev systems, we recommend file mode 666 for `/dev/kbus[0-9]*`.
    * _Note that device nodes may be created on the fly via an ioctl!_ For this reason we recommend a `udev` rule along the lines of the one created by the kernel module makefile.
  1. Build the C library and utilities.
```
make
```
  1. Optionally, run the tests. You will need to have the `python-nose` package installed.
```
cd python
nosetests -d kbus
cd ..
```
    * _Note_ that the test will attempt to use **`sudo`** to insert and remove the module from the running kernel several times, and will complain if it cannot.
    * _Note_ that the tests deliberately behave badly to test error conditions. Do not be alarmed by complaints appearing in your kernel log.
  1. Choose your language and, if necessary, build its interface.
```
make -C cppkbus # C++
# OR #
make -C jkbus # Java
```
    * The C library can be found in `libkbus`.
    * The C++ library in `cppkbus` does _not_ depend on `libkbus`.
    * The Java library "`jkbus`" builds `libkbus` into a JNI shared library. It requires a full JDK in order to build.
    * The Python bindings in do not need to be built; just add the `python` directory to your `PYTHONPATH`.
  1. Go and use kbus in your software...

Unfortunately there aren't many good examples at the moment. While `python/kbus/test/test_kbus.py` gives the kernel module a good shake-down, it's not really didactic as it deliberately behaves badly in order to confirm the correct error behaviour...!

**Note:** If you plan to hack on the kernel module, we strongly recommend you create a virtual machine so you can play with it without having to reboot your entire machine if something goes wrong. (Trust us on this one, we've had to do it a lot.)