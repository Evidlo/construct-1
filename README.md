```
---------------------------------------------------------------------------------------------------
|                                                                                                 |
| THE CONSTRUCT HAS NOT BEEN RELEASED FOR PUBLIC USE. THIS IS FOR DEVELOPERS AND DEMONSTRATION    |
| ONLY. IT IS NOT COMPLETE AND REQUIRES EXPERT KNOWLEDGE TO USE. YOU ARE STILL ENCOURAGED TO TRY  |
| THIS SOFTWARE AND HELP US, BUT IN AN EXPERIMENTAL SETTING ONLY.                                 |
|                                                                                                 |
---------------------------------------------------------------------------------------------------
```

# This — is The **Construct**

<a href="share/webapp">
	<img align="right" src="https://i.imgur.com/TIf8kEC.png" />
</a>

**Fast. Secure. Feature Rich. Community Lead.**

IRCd was a free and open source server which facilitated real-time communication over the
internet. It was started by Jarkko Oikarinen in 1988 at the University of Oulu and [its
derivatives](https://upload.wikimedia.org/wikipedia/commons/d/d8/IRCd_software_implementations.png)
underpinned the major IRC networks for decades.

Due to its age and stagnation since the mid-2000's, a growing number of proprietary cloud services
are now filling the vacuum of innovation. In 2014 a new approach was proposed to reinvigorate
real-time communication for free and open source software: a *federation of networks* known as
*the matrix*.

<h4 align="right">
	IRCd has been rewritten for the global federation of networks &nbsp;&nbsp&nbsp;
</h4>

<a href="https://github.com/vector-im/riot-web/">
	<img align="right" src="https://i.imgur.com/DUuGSrH.png" />
</a>

**This is the Construct** — the community's own Matrix server. It is designed to be
fast and highly scalable, and to be developed by volunteer contributors over
the internet. This mission strives to make the software easy to understand, modify, audit,
and extend. It remains true to its roots with its modular design and having minimal
requirements.

Even though all of the old code has been rewritten, the same spirit and
_philosophy of its predecessors_ is still obvious throughout.

Similar to the legacy IRC protocol's origins, Matrix wisely leverages technologies in vogue
for its day to aid the virility of implementations. A vibrant and growing ecosystem
[already exists](https://matrix.org/docs/projects/try-matrix-now.html).

<h3 align="right">
	Join us in <a href="https://matrix.to/#/#test:zemos.net">#test:zemos.net</a>
	/ <a href="https://matrix.to/#/#zemos-test:matrix.org">#zemos-test:matrix.org</a>
</h3>

## Installation

<a href="https://github.com/tulir/gomuks">
	<img align="right" src="https://i.imgur.com/YMUAULE.png" />
</a>

### Dependencies

- **Boost** library 1.66+
- **RocksDB** library 5.16.6.
- **Sodium** library for curve ed25519.
- **OpenSSL** library for HTTPS TLS / X.509.
- **magic** library for MIME type recognition.
- **zlib** or **lz4** or **snappy** (Optional) Compressions.

##### Build tools

- **GNU C++ compiler**, automake, autoconf, autoconf2.13,
autoconf-archive, libtool.

- A platform capable of loading dynamic shared objects at runtime is required.

<!--

#### Platforms

[![Construct](https://img.shields.io/SemVer/v0.0.0-dev.png)](https://github.com/jevolk/charybdis/tree/master)

| <sub> Continuously Integrated Host </sub>   | <sub> Compiler </sub>    | <sub> Third party </sub> | <sub> Status </sub> |
|:------------------------------------------- |:------------------------ |:------------------------ |:------------------- |
| <sub> Linux Ubuntu 16.04 Xenial </sub>      | <sub> GCC 6       </sub> | <sub> Boost 1.66 </sub>  | [![POSIX Build Status](https://travis-ci.org/jevolk/charybdis.svg?branch=master)](https://travis-ci.org/jevolk/charybdis) |
| <sub> Linux Ubuntu 16.04 Xenial </sub>      | <sub> GCC 8       </sub> | <sub> Boost 1.66 </sub>  | [![POSIX Build Status](https://travis-ci.org/jevolk/charybdis.svg?branch=master)](https://travis-ci.org/jevolk/charybdis) |
| <sub> Linux Ubuntu 18.04 Xenial </sub>      | <sub> GCC 6       </sub> | <sub> Boost 1.66 </sub>  | [![POSIX Build Status](https://travis-ci.org/jevolk/charybdis.svg?branch=master)](https://travis-ci.org/jevolk/charybdis) |

-->


### DOWNLOAD

At this phase of development the best thing to do is pull the master branch
and use the latest head.

> The head of the `master` branch is consistent and should be safe to pull
without checking out a release tag. When encountering a problem with the latest
head on `master` that is when a release tag should be sought.

### BUILD

*Please follow the standalone build instructions in the next section until this
notice is removed.*

```
./autogen.sh
./configure
make
sudo make install
```

Additional documentation for building can be found in [doc/BUILD.md](doc/BUILD.md)

### BUILD (standalone)

This section is intended to allow building with dependencies that have not
made their way to mainstream systems. Important notes that may affect you:

- GCC: Ubuntu Xenial (16.04) users must use a PPA to obtain GCC-7 or greater; don't
forget to `export CXX=g++-7` before running `./configure` on that system.

- Boost: The required version is available through `apt` as `boost-all-dev` on
Ubuntu Cosmic (18.10). All earlier releases (including 18.04 LTS) can configure
with `--with-included-boost` as instructed below (or obtain that package instead).

- OpenSSL: We use 1.0.x for now. Systems that default to 1.1.x will need to
`./configure` with options that find 1.0.x files. Arch Linux users can use
`./configure --with-ssl-includes=/usr/include/openssl-1.0`

- RocksDB: All users should configure with `--with-included-rocksdb` as
instructed below.

#### STANDALONE BUILD PROCEDURE

```
./autogen.sh
mkdir build
```

- The install directory may be this or another place of your choosing.
- If you decide elsewhere, make sure to change the `--prefix` in the `./configure`
statement below.

```
./configure --prefix=$PWD/build --with-included-boost --with-included-rocksdb
```
- The `--with-included-*` will fetch, configure **and build** the dependencies included
as submodules.

```
make install
```

### SETUP

- For standalone builds you will need to add the included lib directories
in your git repo to the library path:
`export LD_LIBRARY_PATH=/path/to/src/deps/boost/lib:$LD_LIBRARY_PATH`
`export LD_LIBRARY_PATH=/path/to/src/deps/rocksdb:$LD_LIBRARY_PATH`

- We will refer to your server as `host.tld`. For those familiar with matrix:
this is your _origin_ and mxid `@user:host.tld` hostpart. If you delegate
your server's location to something like `matrix.host.tld:1234` we refer to
this as your _servername_.

> Construct clusters all share the same _origin_ but each individual instance
of the daemon has a unique _servername_.


1. Execute

	There are two arguments: `<origin> [servername]`. If the _servername_
	argument is missing, the _origin_ will be used for it instead.

	```
	bin/construct host.tld
	````
	> There is no configuration file.

	> Log messages will appear in terminal concluding with notice `IRCd RUN`.


2. Strike ctrl-c on keyboard
	> The command-line console will appear.


3. Create a general listener socket by entering the following command:

	```
	net listen matrix * 8448 privkey.pem cert.pem chain.pem
	```
	- `matrix` is your name for this listener; you can use any name.
	- `*` and `8448` is the local address and port to bind.
	- `privkey.pem` and `cert.pem` and `chain.pem` are paths (ideally
	absolute paths) to PEM-format files for the listener's TLS.

	> The Matrix Federation Tester should now pass. Browse to
	https://matrix.org/federationtester/api/report?server_name=host.tld and
	verify `"AllChecksOK": true`

4. To use a web-based client like Riot, configure the "webroot" directory
to point at Riot's `webapp/` directory by entering the following:
	```
	conf set ircd.webroot.path /path/to/riot-web/webapp/
	mod reload webroot
	```

6. Browse to `https://host.tld:8448/` and register a user.

#### Additional Notes

##### Recovering from broken configurations

If your server ever fails to start from an errant conf item: you can override
any item using an environmental variable before starting the program. To do
this simply replace the '.' characters with '_' in the name of the item when
setting it in the environment. The name is otherwise the same, including its
lower case.

##### Recovering from database corruption

In very rare cases after a hard crash the journal cannot completely restore
data before the crash. Due to the design of rocksdb and the way we apply it
for Matrix, data is lost in chronological order starting from the most recent
transaction (matrix event). The database is consistent for all events up until
the first corrupt event, called the point-in-time.

When any loss has occurred the daemon will fail to start normally. To enable
point-in-time recovery use the command-line option `-pitrecdb` at the next
invocation.

## Developers

<a href="https://github.com/mujx/nheko">
	<img align="right" src="https://i.imgur.com/GQ91GOK.png" />
	<br />
</a>

[![](https://img.shields.io/badge/License-BSD-brightgreen.svg)]() [![](https://img.shields.io/badge/PRs-welcome-brightgreen.svg?style=flat-square)]()

 Generate doxygen using `/usr/bin/doxygen tools/doxygen.conf` the target
 directory is `doc/html`. Browse to `doc/html/index.html`.

## Plan

#### Roadmap for service

- [x] **Phase One**: Matrix clients using HTTPS.
- [ ] **Phase Two**: Legacy IRC network TS6 protocol.
- [ ] **Phase Three**: Legacy IRC clients using RFC1459 / RFC2812 legacy grammars.

#### Roadmap for deployments

The deployment mode is a macro of configuration variables which tune the daemon
for how it is being used. Modes mostly affect aspects of local clients.

- [x] **Personal**: One or few users. Few default restrictions; higher log output.
- [ ] **Company**: Hundreds of users. Moderate default restrictions.
- [ ] **Public**: Thousands of users. Untrusting configuration defaults.

#### Roadmap for innovation

- [x] Phase Zero: **Core libircd**: Utils; Modules; Contexts; JSON; Database; HTTP; etc...
- [x] Phase One: **Matrix Protocol**: Core VM; Core modules; Protocol endpoints; etc...
- [ ] Phase Two: **Construct Cluster**: Kademlia sharding of events; Maymounkov's erasure codes.
