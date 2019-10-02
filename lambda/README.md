Run `mkzip.sh` to create a lambda function out of bamcount.

Pass in the path to a BED file of exon annotations unless it's already been linked as exons.bed in the current working directory.

This will compile the static build of bamcount (if not already present in the parent directory).
It will also setup a local virtualenv of python and pip install awscli.

