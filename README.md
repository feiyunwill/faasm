# Faasm

Faasm is a serverless system focused on performance and security. By trusting users' code we can reduce
isolation, hence achieving good performance while simultaneously enabling features not possible in a 
strongly isolated environment.

The project is still a work in progress with many aspects yet to be developed.

# Isolation

## Memory

By accepting only functions that can be compiled to WebAssembly, we can rule out a large class of security 
issues related to memory. The specifics of the WebAssembly memory model and its security implications
can be found in [the WebAssembly docs](https://webassembly.org/docs/security/).

## CPU

Linux cgroups support process-level and thread-level CPU control and accounting. They can be enlisted to
ensure fair access to CPU resource between functions sharing a Faasm node. Basic cgroup CPU limiting can ensure
that _all user functions get a fair share of the CPU resource on the machine_. This means that if a box is
resource constrained, all functions running there will be restricted equally.

Thread-level support for CPU isolation is available in both V1 and V2 of cgroup although V2 achieves
it differently and is arguably a bit cleaner. Unfortunately Docker doesn't work with V2 at the time of
writing.

## Network

To isolate networking we can take an approach similar to that used in LXC to isolate containers. Each function has its own virtual network interface and operates in its own network namespace. 

## Filesystem

A filesystem doesn't make much sense in a serverless environment given that functions may run on any machine 
in the system. As a result the role of the filesystem is greatly diminished.

## Other I/O

I/O other than that concerned with the filesystem and network is not supported in a serverless environment thus can be excluded. 

# Code Generation

Under the hood WAVM uses the following approach for executing wasm:

- Take in wasm binary or text format
- Translate into LLVM IR
- Use LLVM back-end to generate machine code

In a FaaS environment we have the luxury of knowing about the code up front, so can generate much of the machine code offline. This means upload looks like:

- User uploads wasm binary
- This is converted to LLVM IR
- LLVM IR is used to generate object files
- Object files are stored for future use

When a function is invoked:

- wasm is parsed again (to get all the function definitions, memory regions etc.)
- Object files are loaded (avoids needing to do the IR -> machine code step again)
- Memory is instantiated, object files are linked
- Code is executed

## Unresolved Imports and Stubs

Sometimes wasm code contains calls to functions that don't exist (i.e. unresolved imports). In this scenario we generate a "stub" which bombs out if it's called. To hook this into the pipeline outlined above we need to generate object files that can be linked at runtime. 

Generating these files can take a while if there are lots of them, so we store the generated object files for these stubs.

Each stub function still needs to have the correct signature to be linked properly, so there will be an object file generated for each different combination of return/ parameter types.

# Usage

## Functions

Each function is associated with a user and has a function name. It will have two URLs:

- Synchronous - `<faasm_host>/f/<user>/<function>/`
- Asynchronous - `<faasm_host>/fa/<user>/<function>/`

By `POST`ing to these URLs we can invoke the function. POSTed data forms the input data for the function call.

For example, with the faasm endpoint at `localhost:8080`, the `echo` function owned by `simon` can be run with:

```
curl -X POST http://localhost:8080/f/simon/echo -d "hello faasm"
```

This function just returns its input so should give a response containing our input data (i.e. `hello faasm`).
The code can be found in `func/function_echo.c`.

## Writing Functions

The function API passes a number of pointers to functions to allocate memory regions. A convenience header is
provided in at `include/faasm/faasm.h`. Functions will look something like this:

```
#include "faasm.h"

int exec(struct FaasmMemory *memory) {
    // Do something

    return 0;
}
```

### `faasm.h`

`faasm.h` contains some useful wrappers to make it easier to interact with the Faasm system.

The `FaasmMemory` struct represents the memory available to Faasm functions. It has the following fields:

- `input` - this is an array containing the input data to the function
- `output` - this is where the function can write its output data
- `chainFunctions`, `chainInputs`, `chainCount` - these are internal values used to handle function "chaining" (see below)

### Chaining

"Chaining" is when one function makes a call to another function (which must be owned by the same user).

To do this, `chainFunction()` in `faasm.h` can be called. For my function to invoke the function `foo`,
(also owned by me), it can do the following:

```
#include "faasm.h"

int exec(struct FaasmMemory *memory) {
    uint8_t funcData[] = {1, 2, 3, 4};
    int dataLength = 4;
    char* funcName = "foo";

    chainFunction(memory, funcName, funcData, dataLength);

    return 0;
}
```

`chainFunction` can be called multiple times in one function. Once the original function has completed, these
calls will go back through the main scheduler and be executed.

### Toolchain

Faasm does not support Emscripten, instead we focus on the LLVM/ clang toolchain. This is all built as part of the [WebAssembly waterfall](https://github.com/WebAssembly/waterfall). This will build things like clang, lld, musl etc. and is a submodule of this project.

A tarball of the toolchain is held on S3 and can be downloaded with the `download_toolchain.py` script.

To build the full toolchain (LLVM, Clang, compile-rt, musl), you can use the make target.
Note that this takes **ages** as it's compiling everything from scratch (it also requires subversion to be installed):

```
make setup-tools
```

This is currently required as LLVM wasm support is only experimental. In future it may be bundled with LLVM/ clang normally.

If you want to update the tarball of the toolchain once it's built, you can run the following, then upload to S3:

```
make tar-tools
```

## Uploading Functions

To upload a function you can use `curl` to send a PUT request to the synchronous URL for the given function.
For example:

- I have a Faasm endpoint running at `localhost:8080`
- I've compiled my WebAssembly function file to `/tmp/do_something.wasm`
- I want to upload this function to user `simon` and function name `cool_func`

I can execute:

```
curl http://localhost:8080/f/simon/cool_func/ -X PUT -T /tmp/do_something.wasm
```

# Kubernetes

Faasm can be deployed to Kubernetes to create a distributed set-up. The configuration files are found in the `k8s`
directory. This has the following components:

- A load balancer handling incoming calls
- A single `edge` pod which parses the calls and input data
- A `redis` pod which holds the function calls in a queue
- Multiple `worker` pods which pull calls off the queue, execute them (and any chained calls), then put the results into another Redis queue.

## Using a Kubernetes deployment

The quickest way to play with a Kubernetes deployment is to SSH onto the master node and get the URL for the Faasm edge service:

```
kubectl get service edge --namespace=faasm
```

You can then hit one of the dummy functions with:

```
curl -X POST http://<endpoint_ip>:8080/f/simon/echo/ -d "hello world"
```

## Deploying to Kubernetes

Deployment to Kubernetes is handled via the make target:

```
make setup-k8
```

## Function storage

Clearly all worker pods need access to the WASM function files. For now these are held on an NFS share hosted on the
master Kubernetes node. This could be replaced by an object store in future.


# Installation, Configuration and Development

Below are instructions for building, testing and developing.

The local development process is a bit rough around the edges at the moment but can be improved in future.

## Ansible 

[Ansible](https://www.ansible.com/) is required to deploy and set up the project. The easiest way to install is to use the Python package manager, `pip`:

```
sudo pip install -U ansible
```

Make sure you have version >2.6

## Networking

### Set-up

First of all we want't to ensure consistent interface naming (`eth0` etc.) so you need to:

- Edit `/etc/default/grub` and add `net.ifnames=0 biosdevname=0` to `GRUB_CMDLINE_LINUX_DEFAULT`
- Run `sudo update-grub`

We then need to configure some network interfaces and namespaces. This is done via a make target:

```
make setup-network
```

### Ubuntu 18.04 and `netplan`

If you're using Ubuntu 18.04, I've not yet configured the project for `netplan` so we revert back to `ifupdown`:

- Edit `/etc/default/grub` and add `netcfg/do_not_use_netplan=true` to `GRUB_CMDLINE_LINUX` (not `GRUB_CMDLINE_LINUX_DEFAULT`)
- Run `sudo update-grub`
- Run `apt install ifupdown`
- Reboot

### Ubuntu desktop and NetworkManager

To avoid conflicts you may need to edit your `/etc/NetworkManager/NetworkManager.conf` to include:

```
[main]
plugins=ifupdown,keyfile

[ifupdown]
managed=false
```

## Submodules

Faasm relies on some submodule which may need to be updated or cloned

```
# Update submodules
git submodule update --remote --init
```

If you want to make changes to submodules and push, you need to do the following:

- Make the changes in the submodule
- Change to the submodule directory, commit and push (as if it were a normal repo)
- Run `git submodule update --remote --merge` in the root of this project
- Commit and push the root repo

## Libraries

Faasm has various library dependencies that can be installed with Ansible using the relevant `make` target:

```
make setup-libs
```

## Protobuf

Faasm depends on protobuf which can be a bit of a hassle to install. First of all you can try the make target:

```
make setup-protobuf
```

If there are any issue you need to remove every trace of protobuf on your system before reinstalling.

You can look in the following folders and remove any reference to `libprotobuf` or `protobuf`:

- `/usr/lib/x86_64-linux-gnu/`
- `/usr/lib/`
- `/usr/include/google`

Avoid trying to do this with `apt` as it can accidentally delete a whole load of other stuff.

## Clang

So far Faasm has only been built with clang (although there's no reason not to use something else).
There's a make target for installing clang at:

```
make setup-clang
```

## Redis

At the moment Faasm uses Redis for messaging between the edge and worker nodes. For running locally you'll need to
install it on your machine:

```
sudo apt install redis-server redis-tools
```

## Building with CMake

Faasm also requires an up-to-date version of CMake. Once you have this you can run the following from the root of
your project:

```
mkdir build
cd build
cmake -DCMAKE_CXX_COMPILER=/usr/bin/clang++ -DCMAKE_C_COMPILER=/usr/bin/clang -DCMAKE_BUILD_TYPE=Release ..
cmake --build . --target all
```

I usually develop through CLion which makes it a lot easier.

## Running Locally

Once things are built, you can run a simple local set-up with:

```
# Set up function storage path (see below)
export FUNC_ROOT=<path to the root of this project>

# Edge node listening on localhost:8080
./build/bin/edge

# Worker
./build/bin/worker
```

The `FUNC_ROOT` is where Faasm will look for function files. For a function called `dummy` owned by user
`simon` the `.wasm` file should be found at `$FUNC_ROOT/wasm/simon/dummy/function.wasm`. When you upload a function
this is where it will get placed too.

## Running Faasm Functions Natively

To aid debugging, it's easiest to run functions outside of Faasm, i.e. compile the C code on your machine
and execute it natively. By doing this you can make sure your function works in a normal environment before
compiling it to wasm.

To do this you can use the `native_func` CMake target. The steps are:

- Put your function file into the `func` directory
- Set `FAASM_FUNCTION` to your function file in `func/CMakeLists.txt`
- Add any required libraries to `func/CMakeLists.txt` in `target_link_libraries`
- Build and run the `native_func` target

## Dummy functions

Currently there are some dummy functions held in the `func` directory. Their compiled WASM is also stored in the
`wasm` directory.

## Docker images

There are a few Docker images used to make build times quicker:

- `shillaker/cpp-base` - from [this repo](https://github.com/Shillaker/cpp-base), just a base image that includes clang and protobuf (as they're a pain to install)
- `shillaker/faasm-base` - image extending `cpp-base` with the extra faasm-specific libraries installed
- `shillaker/faasm-core` - image extending `faasm-base` with the faasm code in place

If you're just changing code, all you need to rebuild is `faasm-core`. If you change libraries you'll need to rebuild
`faasm-base` too.

## Tests

The tests can be found in the `test` directory and executed by running:

```
./build/bin/tests
```

They require a local Redis instance to pass and cover most of the codebase.

## Compilation

I've found the easiest non-Emscripten toolchain to use is [wasmception](https://github.com/yurydelendik/wasmception). 

There's also some Python scripts in the `bin` directory that may prove useful.

## Syscalls

Syscall support is determined by the musl port that we use to compile our code. Currently we're using [my fork](https://github.com/Shillaker/musl) of an experimental (but popular).

The mapping of syscalls is done in [this file](https://github.com/Shillaker/musl/blob/wasm-prototype-1/arch/wasm32/syscall_arch.h) in that repo.

# CGroup V2

To enable `cgroupv2` you need to do the following:

- Add `cgroup_no_v1=all systemd.unified_cgroup_hierarchy=1` to the line starting `GRUB_CMDLINE_LINUX_DEFAULT` in `/etc/default/grub`(separated by spaces)
- Run `sudo update-grub`
- Restart the machine

To check it's worked, the file `/sys/fs/cgroup/cgroup.controllers` should exist and contain something like:

```
cpu io memory pids rdma
```

Note that currently **Docker does not work with cgroupv2** so this is only applicable to machines running Faasm outside of Docker.
