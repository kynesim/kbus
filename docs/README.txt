Pre-built documentation
-----------------------
For your comfort and convenience, a pre-built version of the KBus
documentation is available at:

    http://code.google.com/p/kbus/

or (since you've extracted this from Subversion) it should be available within
this directory at:

    _build/html/index.html

(yes, that name starts with an underscore).

Building the documentation
--------------------------
The KBus documentation is built using Sphinx_.

.. note:: It needs (at least) version 0.6 of Sphinx, which is later than the
          version installed via apt-get on Ubuntu 8.10. The best way to
          upgrade is with easy_install, as described on the Sphinx website.

You also need graphviz (which provides ``dot``).

.. _Sphinx: http://sphinx.pocoo.org/

With luck, the HTML checked out with the KBus sources will be up-to-date, and
you won't need to (re)build the documentation. However, if you should need to
(for instance, because you've updated it), just use the Makefile::

    make html

The Python bindings
-------------------
Read the kbus-kernel-module.txt file to see how individual classes and
functions within kbus.py are documented. Obviously, if you add, remove or
rename such, you may need to alter this file -- please do so appropriately.

Mime type magic
---------------
In order for the documentation in the Google Code Subversion repository to be
useable as documentation, the HTML, CSS and JavaScript files in the _build
directory tree need to have the correct mime types. Subversion is clever
enough to be able to cope with this, and the ``svn_propset`` script shows what
to do (it may be enough to just run it again - I shall update this if/when I
find out).

Subversion gotchas
------------------
Sphinx believes that the contents of ``_build`` are transitory - i.e., that it
is free to delete them if it wishes. In particular, ``make clean`` will delete
all of the contents of ``_build``.

Meanwhile, we've committed ``_build/html`` and its contents to Subversion,
which has created ``.svn`` directories therein. Deleting these, and then
recreating the directories without them (via ``make html``) will confuse
Subversion when it notices this.

The simplest solution is:

a. Don't use ``make clean`` (!)
b. Do use ``svn update`` to retrieve the checked in files/directories if you
   do - with luck the dependency tracking in the ``make`` process will cope.

