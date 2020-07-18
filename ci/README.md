## Gitlab runners

Integration tests run on gitlab runners.
We have two runners: one on a linux VM with a docker executor, one on an OSX
VM with an ssh executor.

To create the VM and runners I had to:
  - Go to ci.inria.fr, login, go to the parcoach project.
  - Create two slaves (one using Ubuntu 18, 2 CPUs, and 2 GB of RAM; one using
  MacOSX Catalina, 2 CPUs, and 2 GB of RAM).
  - Add ssh public key to account.
  - Use the ssh information given when clicking on "connect". Both user and
  password are 'ci' (change the password once logged in).
  - Follow the information [here](http://sed.bordeaux.inria.fr/org/gitlab-ci-nojenkins.html#org7979bbb) to create a *docker* runner.

For OSX, I followed the information [here](https://docs.gitlab.com/runner/install/osx.html).
But starting the runner doesn't quite work with ssh-only VM, so I had to start
it manually.
I first ran a random command with `sudo` so that it doesn't prompt the password
for the next command.
Then I ran `sudo nohup gitlab-runner run --working-directory /builds &`.
The `nohup` part is to let it run when logging out from the ssh session.

## Linux docker image

In order to control the test environment we run our linux tests in a docker
container.
The container runs an ubuntu image where the dependencies are installed, its
setup can be found in the `Dockerfile.parcoach`.

When updating some dependency, make sure to update the tag of the image when
building it.
For instance currently the tag is `llvm9`, if we had to update the llvm version
we would:
  - pull the image with `docker pull registry.gitlab.inria.fr/parcoach/parcoach:llvm9`.
  - update the Dockerfile to install llvm 10.
  - build the image by running - in the `ci` directoy -
  `docker build -t registry.gitlab.inria.fr/parcoach/parcoach:llvm10 . -f Dockerfile.parcoach`
  (notice the tag changed).
  - update the image in the gitlab registry with `docker pull registry.gitlab.inria.fr/parcoach/parcoach:llvm10`.

And update the image used in the `.gitlab-ci.yml`.
