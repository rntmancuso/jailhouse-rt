Cache Coloring Support
======================

Introduction
------------

### Cache partitioning and coloring

#### Motivation

Cache hierarchies of modern multi-core CPUs typically have first levels
dedicated
to each core (hence using multiple cache units), while the last level cache
(LLC) is shared among all of them. Such configuration implies that memory
operations on one core, e.g., running one Jailhouse inmate, are able to generate
timing *interference* on another core, e.g., hosting another inmate. More
specifically, data cached by the latter core can be evicted by cache store
operations performed by the former. In practice, this means that the memory
latency experienced by one core depends on the other cores (in-)activity.

The obvious solution is to provide hardware mechanisms allowing either: a
fine-grained control with cache lock-down, as offered on the previous v7
generation of Arm architectures; or a coarse-grained control with LLC
partitioning among different cores, as featured on the "Cache Allocation
Technology" of the high-end segment of recent Intel architecture and supported
by the Jailhouse hypervisor.

#### Cache coloring

Cache coloring is a *software technique* that permits LLC partitioning,
therefore eliminating mutual core interference, and thus guaranteeing higher and
more predictable performances for memory accesses. A given memory space in
central memory is partioned into subsets called colors, so that addresses in
different colors are necessarily cached in different LLC lines. On Arm
architectures, colors are easily defined by the following circular striding.

```
          _ _ _______________ _ _____________________ _ _
               |     |     |     |     |     |     |
               | c_0 | c_1 |     | c_n | c_0 | c_1 |
          _ _ _|_____|_____|_ _ _|_____|_____|_____|_ _ _
                  :                       :
                  '......         ........'
			. color 0 .
		. ........      ............... .
                         :      :
            . ...........:      :..................... .

```

Cache coloring suffices to define separate domains that are guaranteed to be
*free from interference* with respect to the mutual evictions, but it does not
protect from minor interference effects still present on LLC shared
subcomponents (almost negligible), nor from the major source of contention
present in central memory.

It is also worth remarking that cache coloring also partitions the central
memory availability accordingly to the color allocation--assigning, for
instance, half of the LLC size is possible if and only if half of the DRAM space
is assigned, too.


### Cache coloring in Jailhouse

The *cache coloring support in Jailhouse* allows partitioning the cache by
simply partitioning the colors available on the specific platform, whose number
may vary depending on the specific cache implementation. More detail about color
availability and selection is provided in [Usage](#usage).

#### Supported architectures

Cache coloring is available on Arm architectures. In particular, extensive
testing has been performed on v8 CPUs, namely on the A53 and A57 processors
equipping Xilinx ZCU102 and ZCU104, and NVIDIA Tegra X1 and X2.

Cache coloring support is currently unavailable on x86 machines.

#### Limitations

Since Jailhouse is currently lacking SMMU support, and since the colored memory
mapping must be provided to DMA devices to allow them a coherent memory view,
coloring for this kind of devices is not available.
This also explain why also coloring support for the Linux root cell is not
provided, although possible and tested with a simple hot remapping procedure.

Shared memory regions can be colored as well, but only with great care, and
using the [manual colored allocation](#manual-colored-allocation).


### Further readings

Relevance, applicability, and evaluation results of the Jailhouse cache coloring
support are reported in several recent works. A non-technical perspective is
given in [1] together with an overview of the ambitious HERCULES research
project. A technical and scientific presentation is instead authored in [2],
where additional experimental techniques on cache and DRAM are introduced. A
specific real-time application is extensively discussed in [3].

An enjoyable, comprehensive and up-to-date survey on cache management technique
for
real-time systems is offered by [4].

1. P. Gai, C. Scordino, M. Bertogna, M. Solieri, T. Kloda, L. Miccio. 2019.
   "Handling Mixed Criticality on Modern Multi-core Systems: the HERCULES
   Project", Embedded World Exhibition and Conference 2019.

2. T. Kloda, M. Solieri, R. Mancuso, N. Capodieci, P. Valente, M. Bertogna.
   2019.
   "Deterministic Memory Hierarchy and Virtualization for Modern Multi-Core
   Embedded Systems", 25th IEEE Real-Time and Embedded Technology and
   Applications Symposium (RTAS'19). To appear.

3. I. SaÃ±udo, P. Cortimiglia, L. Miccio, M. Solieri, P. Burgio, C. di Biagio, F.
   Felici, G. Nuzzo, M. Bertogna. 2020. "The Key Role of Memory in
   Next-Generation Embedded Systems for Military Applications", in: Ciancarini
   P., Mazzara M., Messina A., Sillitti A., Succi G. (eds) Proceedings of 6th
   International Conference in Software Engineering for Defence Applications.
   SEDA 2018. Advances in Intelligent Systems and Computing, vol 925. Springer,
   Cham.

4. G. Gracioli, A. Alhammad, R. Mancuso, A.A. FrÃ¶hlich, and R. Pellizzoni. 2015.
   "A Survey on Cache Management Mechanisms for Real-Time Embedded Systems", ACM
   Comput. Surv. 48, 2, Article 32 (Nov. 2015), 36 pages. DOI:10.1145/2830555




Usage
-----

We shall first explain how to properly choose a color assignment for a given
software system.
Secondly, we are going to deep into the root cell configuration, which enables
cache coloring support for inmates.
Lastly, we are explaining how a color selection can be assigned to a given cell
configuration, which also support a simplified "automatic" memory allocation,
along with the ordinary one.

### Colors selection

#### Color bit masks

In order to choose a color assignment for a set of inmates, the first thing we
need to know is... the available color set. The number of available colors can
be either calculated[^1] or read from the handy output given by the Jailhouse
driver once we enable the hypervisor.

```
...
[   19.060586] Coloring: Colors available: 16
[   19.113230] The Jailhouse is opening.
...
```

[^1]: To compute the number of available colors on a platform one can simply
  divide
  `way_size` by `page_size`, where: `page_size` is the size of the page used
  on the system (usually 4 KiB); `way_size` is size of a LLC way, i.e. the same
  value that has to be provided in the root cell configuration.
  E.g., 16 colors on a platform with LLC ways sizing 64 KiB and 4 KiB pages.

Once the number of available colors is known, we can use a bitmask of equal
length to encode a color selection. We are going to use it to inform the
hypervisor of our choice, as later explained in the section about
[cells configuration](#cells-configuration).
It suffices to set all and only the bits having a position equal to the chosen
colors, leaving unset all the others.

For example, if we choose 8 colors out of 16, we can use a bitmask with 8 bits
set and 8 bit unset, like:

- `0xff00` -> `1111 1111 0000 0000`
- `0x0ff0` -> `0000 1111 1111 0000`
- `0xc3c3` -> `1100 0011 1100 0011`
- `0xaaaa` -> `1010 1010 1010 1010`


#### Colors selection and partitioning

We can choose any kind of color configuration we want but in order to have
mutual cache protection between cells, memory regions with different colors must
be assigned to different cells.

Another point to remember is to keep colors as contiguous as possible, so to
allow caches to exploit the higher performance to central memory controller.
Configurations like `0xaaaa` should thus be avoided.


### Root Cell configuration

The root cell configuration allows two coloring-related information to be
specified.

#### LLC way size

This field is _mandatory_ for enabling coloring support.
The value corresponds to the way size in bytes of the Last Level Cache and
could be calulated by dividing the LLC size by the number of way
(the way number refers to N-way set-associativity).

For example, a 16-way set associative cache sizing 2 MiB has a way size of
128 KiB:
```c
...
.platform_info = {
    ...
    .llc_way_size = 0x10000,
    .arm = {
        .gic_version = 2,
        .gicd_base = 0xf9010000,
        .gicc_base = 0xf902f000,
        .gich_base = 0xf9040000,
...
```

#### Managed-allocation colored region

This _optional_ information is defined as a "special" colored memory region that
will be used by the [automatic colored allocation](#cells-configuration), which
drastically eases the configuration. The memory can be defined as follows:
```c
/* Colored RAM for inmates */ {
    .phys_start = 0x801000000,
    .virt_start = 0x801000000,
    .size =        0x7fa00000,
    .flags = JAILHOUSE_MEM_READ | JAILHOUSE_MEM_WRITE |
        JAILHOUSE_MEM_EXECUTE | JAILHOUSE_MEM_COLORED,
},
```
Note that the only configuration to add to an ordinary memory region definition
is the `JAILHOUSE_MEM_COLORED` flag.

N.B. If there is no colored region defined in the root cell configuration
the automated colored allocation can not be used.

#### Advanced configurations

- Page pool usage

  Using colored memory has its drawbacks. One of them is the amount of page
  entries that is needed by the strided memory mapping. This could easliy fill
  up all the memory reserved for Jailhouse.

  In that case, this problem can be easily circumvented by increasing the
  hypervisor memory size in the root cell. For example:
    ```c
    .hypervisor_memory = {
        .phys_start = 0x800000000,
        .size =       0x001000000,
    },
    ```

- Big page size

  By default, the cache coloring support allows 4 KiB page (always the case in
  Arm v7 and default in v8) for colored mapping and this value is synchronized
  with the hypervisor page size definition. If a bigger page size is needed, it
  must be be specified in `jailhouse/include/coloring.h` accordingly to
  Jailhouse/Linux.

  It is should be remarked that increasing the page size reduces the number of
  available colors. Each additional address bit used for the page offset is
  directly taken from the address bit available for coloring -- doubling the
  page size halves the colors.


### Cells configuration

To add a colored memory region in a cell configuration: the
`JAILHOUSE_MEM_COLORED_CELL` flag has to be added to a standard memory region,
and the color selection bitmask must be defined in the `colors` struct member.
It is, of course, very important to use a bitmask with a number of bits at most
equal to available colors. Any exceeding bit will be ignored.

Currently, we support two types of non root-cells configuration:
automated and automatic.


#### Automated colored allocation

In order to enable the simplified configuration interface to memory allocation,
in the root cell configuration a unique memory region needs to be flagged as
`JAILHOUSE_MEM_COLORED`, as [previously](#root-cell-configuration) explained.
Once this requirement is satisfied, multiple cell definitions with `colored`
memory spaces can be created by simply informing Jailhouse of the colors
selection -- the memory space is assumed to always be the same one just
mentioned.

For instance:
```c
/* Colored RAM */ {
    .virt_start = 0,
    .size = 0x800000,
    .flags = JAILHOUSE_MEM_READ | JAILHOUSE_MEM_WRITE |
        JAILHOUSE_MEM_EXECUTE | JAILHOUSE_MEM_LOADABLE |
        JAILHOUSE_MEM_COLORED_CELL,
    .colors = 0xff00,
},
```
It can be noticed that no physical start address is needed -- it actually *must*
be removed from the memory configuration, as in the example. The coloring
support in the driver interface will take care of this.

Using this configuration, the problem of overlapping colored memories will be
considerabily reduced.


#### Manual colored allocation

This configuration is intended to be used by more advanced users, since no
controls will be made when defining a colored memory region. In this case, one
can define it as the follow:
```c
/* Colored RAM */ {
    .phys_start = 0x810000000,
    .virt_start = 0,
    .size = 0x800000,
    .flags = JAILHOUSE_MEM_READ | JAILHOUSE_MEM_WRITE |
        JAILHOUSE_MEM_EXECUTE | JAILHOUSE_MEM_LOADABLE |
        JAILHOUSE_MEM_COLORED_CELL,
    .colors = 0xff00,
},
```

In this case, contrarily to the automated allocation, the physical start address
is mandatory; otherwise the automated interface will be used, if present.

N.B. If a managed-allocation colored region is defined in the root cell,
one cannot define in a cell configuration a colored memory region that overlaps
it. In that case the cell creation will be aborted.
