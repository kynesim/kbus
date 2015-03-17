KBUS is a lightweight messaging system for Linux, particularly aimed at embedded
platforms. Message passing is managed by a kernel module, via reading/writing
'/dev/kbus0' style devices. Python bindings are provided along with libraries
for C, C++ and Java.

You might want to look at:

* The [specification][1]. Start reading here to learn more about KBUS and how
  you might use it.
* [Getting Started][2] - what source to download, how to build it and get to the
  point where you can use KBUS in your own software.
* The full [documentation][3].
* [Development History][4].

Status
------

KBUS is considered stable; we use it for a number of client projects.

Past development work was concentrated on:

* peer-to-peer messaging support (allowing messages to be sent from one KBUS
device to another, on the same or a different machine).
* tidying up the kernel module, with the idea of submitting it to Linux (which
was, in fact, quite instructive)

Current development work is more oriented towards:

* better integration with the [muddle][5] build system
* in-kernel builds
* updating the documentation

---

The KBUS [documentation][3] is rebuilt whenever a push is made to the
repositories. It is hosted by [Read the Docs][6], who are wonderful people.

[1]: http://readthedocs.org/docs/kbus/en/latest/specification.html
[2]: https://github.com/kynesim/kbus/wiki/Getting-Started
[3]: http://kbus.readthedocs.org/
[4]: https://github.com/kynesim/kbus/wiki/Development-History
[5]: https://github.com/kynesim/muddle
[6]: http://readthedocs.org/
