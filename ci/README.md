## Gitlab runners

Integration tests run on gitlab runners and we currently use the Inria CI
shared runners, which are more than enough at the moment.

If needed we could create more runners in a linux VM with a docker executor.
It would basically come down to going to ci.inria.fr, signing in, creating
the PARCOACH project, and following instruction to register a runner.

## Linux docker images

In order to control the test environment we run our linux tests in a docker
container.
The container runs an ubuntu image where the dependencies are installed, its
setup can be found in the `Dockerfile.parcoach`.

The default runner environment is not enough to be able to build docker images,
therefore we also have a very small `Dockerfile.builder` which creates a basic
image to create images.

Creating this basic image is not automatic, and it can be created by running
the following command from this folder (the `ci` one):
```bash
docker build -t registry.gitlab.inria.fr/parcoach/parcoach:builder -f Dockerfile.builder .
docker push registry.gitlab.inria.fr/parcoach/parcoach:builder
```

From there the generation of docker images is automatic based on the image
name: if the image name is `ubuntu-focal-build-20220617-llvm-12` it will create
an image based on Ubuntu Focal, containing the build tools for the `20220617`
version described in the [build-image](./build-image) script, and with LLVM
12 installed.
Updating the image to include LLVM 13 is as simple as bumping the version in the
image name.

Once you've figured out the image name, building the docker image is as simple
as executing the following command from this folder:
```bash
./build-image parcoach:ubuntu-focal-build-20220617-llvm-12 -f Dockerfile.parcoach
```

You can also check the [.gitlab-ci.yml](../.gitlab-ci.yml) file which defines
the job `build-linux-image` and automatically creates the docker image for the
tests if it doesn't exist in the registry.

### Developer notes

While it's not necessary to understand how it works internally to update the
images, here are some more insight about how everything works.
Let's assume the image tag is `ubuntu-focal-build-20220617-llvm-12`; the script
will do the following:
  - split the image name so that we can deal with a pair `(tool,tool_version)`,
  in this case: `(ubuntu,focal),(build,20220617),(llvm,12)`.
  - for each pair it will emit a `--build-arg` passed down to docker. For single
  targets, like `llvm` it will simply emit `--build-arg LLVM_VERSION=12`.
  For "meta" targets, like the `build` which regroups all build tools, it will
  emit all versions described in the [build-image](./build-image) script (in
  this case: `--build-arg CMAKE_VERSION=3.23.2 --build-arg CMAKE_SHA256=...`,
  because the only necessary tool is CMake at the time of writing this
  document.
  - finally the [Dockerfile.parcoach](./Dockerfile.parcoach) leverages these
  versions information to build the image layer by layer (which makes reusing
  layers very straightforward).

Adding a specific tool for a specific target (eg: OpenMPI, Fortran) is as simple
as adding an other installation script layer in the Dockerfile, and including
the tool name in the tag (eg: `ubuntu-focal-build-20220617-llvm-12-fortran-12`).
