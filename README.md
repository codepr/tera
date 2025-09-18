Tera
====

# Data-Oriented MQTT Broker

This project is an extremely lightweight, high-performance MQTT broker designed
for resource-constrained and real-time environments. It is built from the
ground up using **data-oriented programming (DOP)** principles to achieve
maximum efficiency and predictability.

The purpose of the project is mainly to explore and get some hands-on DOD style
and trying to achieve some good performance on a rather small system.

**Project Status:** Pre-Alpha. This project is not ready to be used. It doesn't
even support all messages and session persistence yet.  There are an exhaustive
amount of robust solutions in the IoT domain, Tera doesn't aim to enter that
market or be production ready anytime soon.

## Core Design Philosophy

The broker's architecture is a deliberate divergence from traditional,
object-oriented designs. Key tenets include:

  * **No Runtime Allocations**: All memory is pre-allocated at startup through a large,
      flat arena or fixed-size arrays. This eliminates the unpredictable latency and overhead
      associated with `malloc` and `free` operations.
  * **Cache Locality**: Data is organized in a **Structure of Arrays (SoA)** style to ensure
      that related information is stored contiguously in memory. This maximizes the utilization
      of the CPU's cache, leading to significant performance gains during critical operations
      like topic matching and session management.
  * **Simplicity and Predictability**: The broker is designed to be a fast, deterministic engine
      for MQTT data. Complex features that would introduce performance variability (e.g., dynamic
      resizing, large message support) are intentionally excluded.

## Tradeoffs

To ensure predictability and performance, some constraint had to be put in
place, at least for a very first working version, in the future there will be
room for different approaches maybe.

The official [MQTT spec](https://docs.oasis-open.org/mqtt/mqtt/v5.0/mqtt-v5.0.html) ensure
a very lean and lightweight protocol design by default, however the actual load
technically allowed can be relatively heavy (e.g. in terms of small devices
and embedded), for example:

- **Max connections:** MQTT spec does not define a hard maximum number of clients.
  It’s implementation-dependent (limited by OS/network resources and broker implementation).
  So in theory: unlimited, in practice: thousands to millions depending on broker.
- **Strings, IDs, Topic Names:** All the strings are represented as UTF-8 with a 16 bytes
  prefixed length, which means, possibly, 65536 bytes each,
  Applies to ClientID, Topic Names, User Properties, Reason Strings, etc.
- **Payload size:** Remaining length can be encoded into up to 4 bytes, using 1 bit for
  continuation in each, which means up to ~256 MB.

To be fully compliant with these numbers, memory allocation is not really
avoidable, could design some smarter "lazy" allocation heuristics that stretch
and shrink based on the load but that would add a kind of complexity that's out
of the scope of this small project, so the final choice will be, driven by a
config file, to run it with a default, way lighter setting, based on the
following, roughly estimated premises:

- **Memory budget:** Assume in the order of 1-5 MB
- **Per-client overhead:** A few hundred of bytes of metadata
- **Number of active clients:** In the range of 100 - 1000, allowing for a small memory
  footprint and keeps data within CPU caches (L1, L2, L3) for optimal performance
- **Max payload size:** A limit of 1 KB to 4 KB per message would be very reasonable.
  This accommodates common use cases like sensor data, control commands, and simple status updates.
- **Larger payloads handling:** Simply reject any incoming message that exceeds this pre-defined limit.
  If in need of larger data, like firmware updates or images, a different protocol would be suggesed or
  break the data into smaller, manageable chunks and reassemble them on the client side.


## Architectural Components

The broker's state is managed through a series of pre-allocated, flat data structures:

  * **Server Context**: A central struct that acts as a handle to all pre-allocated resources,
    passed to all processing functions. This avoids reliance on global variables.
  * **Connection Arena**: A pool of structs for managing TCP socket information, I/O buffers,
    and connection state.
  * **Client Arena**: A pool for managing MQTT session data, including keepalive times,
    clean session flags, and client IDs.
  * **Topic Arena**: A large bump-allocated buffer for storing all topic strings from
    subscriptions and publications.
  * **Message Arena**: A bump-allocated buffer for storing the payloads and topics of all
    active messages.
  * **Metadata Arrays**: Fixed-size arrays that hold metadata (e.g., offsets, sizes, flags)
    for messages and subscriptions, acting as indices into the main arenas.

## How to Build

The project uses a standard Makefile.

```sh
make
```

## Roadmap

There is a small working core at the moment, with a handful of basic features, planned work
to enrich it

- Support for all the MQTTv5 features ❌
    - Proper topic support, wildcards etc ❌
    - ...
- Move memory allocation to zero'ed heap memory  ❌
- Allow to compile with no heap memory allocation  ❌
- Configuration handling  ❌
- Session management and persistence ❌
