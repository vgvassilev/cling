# Docker Build for Cling

* Dockerized build of [Cling](https://root.cern.ch/cling)
* Based on Ubuntu 14.04 (can be changed in the [`Dockerfile`](./Dockerfile))

## Usage

1. [Install docker](https://docs.docker.com/engine/installation/)
2. Build the docker image yourself or get one from docker hub
3. Use it like any other docker image

### Build the Image

```bash
cd tools/docker
# depending on your machine, this might take a while to finish building
docker build -t my_cling_image .
# run it ! (the entry point is cling)
docker run -it my_cling_image
```

### Get a Prebuilt Image from Docker Hub

```bash
# get the build from docker hub
docker pull maddouri/cling-ubuntu-docker
# run it ! (the entry point is cling)
docker run -it maddouri/cling-ubuntu-docker
```


## Notes

### Defining Aliases

```bash
alias cling='docker run -it my_cling_image'
```

### Working with Files

#### Acessing the File System

As with any other Docker images, you can access your file system from the container by attaching volumes to it:

Syntax:

```bash
docker run -v /path/to/host/folder:/path/to/container/folder -it my_cling_image
```

Usage example:

```bash
$ ls /media/data/myCode
func.cpp

$ cat /media/data/myCode/func.cpp
#include <iostream>
void sayHi() {
    std::cout << "Hello Dockerized Cling !" << std::endl;
}

$ docker run -v /media/data/myCode:/code -it my_cling_image
****************** CLING ******************
* Type C++ code and press enter to run it *
*             Type .q to exit             *
*******************************************
[cling]$ .L /code/func.cpp
[cling]$ sayHi()
Hello Dockerized Cling !
[cling]$ .q
$ # back to the host machine
```

#### Using Pipes

```bash
# NB: use "-i" instead of "-it" when piping
echo -e '#include <iostream>\n std::cout <<  "Hello Dockerized Cling !" << std::endl;' | docker run -i my_cling_image
```

### Which Version of Cling is Available in the Docker Image ?

When building an image, [`build-cling.sh`](./build-cling.sh) clones the latest commit available from [CERN's repository](https://root.cern.ch/gitweb/?p=cling.git;a=summary).

The exact commit SHA1 can be found in the `${CLING_COMMIT_SHA1}` file:

```bash
docker run -it --entrypoint=/bin/bash my_cling_image
cat ${CLING_COMMIT_SHA1}
```
