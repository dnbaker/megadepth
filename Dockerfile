FROM centos:7

# Install basics
RUN yum install -y cmake gcc gcc-c++ autoconf make libstdc++-static zlib-devel curl-devel glibc-static bzip2 git wget python2.7 awscli

#now deploy Snaptron server proper
RUN mkdir -p /deploy
RUN git clone https://github.com/ChristopherWilks/bamcount.git -b bw_read /deploy/bamcount
RUN cd /deploy/bamcount && /bin/bash -x build_no_container.sh
RUN cd /deploy/bamcount && /bin/bash -x build_no_container.sh static

# Define environment variable
ENV NAME World

CMD /deploy/bamcount/bamcount_static
