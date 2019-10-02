
function handler () 
{
    #go to where it's writable
    echo $LAMBDA_TASK_ROOT
    export annotation="$LAMBDA_TASK_ROOT/exons.bed"
    #S3 path to BAM file
    bam=$1
    #TODO: get s3 path to annotation and download it to /tmp
    #maybe already package an existing annotation but check to see if it's changed
    #get friendly name for output
    id=$(echo "$(basename $bam)" | cut -d'.' -f 1)
    rm -rf /tmp/${id}
    mkdir /tmp/${id}
    cd /tmp/${id}
    result="$(mktemp)"
    #cat $bam | ./bamcount_static - --threads 4 --coverage --no-head --bigwig $id --auc $id --annotation $annotation $id --frag-dist $id > $result 2>&1
    #./aws --region us-east-2 --profile jhu-langmead s3 cp $bam - | ./bamcount_static - --threads 4 --coverage --no-head --bigwig $id --auc $id --annotation $annotation $id --frag-dist $id > $result 2>&1
    #python ${LAMBDA_TASK_ROOT}/aws --region us-east-2 --profile jhu-langmead s3 cp $bam - | ${LAMBDA_TASK_ROOT}/bamcount_static - --threads 4 --coverage --no-head --bigwig $id --auc $id --annotation $annotation $id --frag-dist $id > $result 2>&1
    #the actual lambda command
    python ${LAMBDA_TASK_ROOT}/aws s3 cp $bam - | ${LAMBDA_TASK_ROOT}/bamcount_static - --threads 4 --coverage --no-head --bigwig $id --auc $id --annotation $annotation $id --frag-dist $id > $result 2>&1
    #python ${LAMBDA_TASK_ROOT}/aws --region us-east-2 --profile jhu-langmead s3 cp --recursive /tmp/${id} s3://bamcount-output/${id}
    python ${LAMBDA_TASK_ROOT}/aws s3 cp --recursive /tmp/${id} s3://bamcount-output/${id}
    #python ${LAMBDA_TASK_ROOT}/aws s3 cp ${id}.frags.tsv ${id}.auc.tsv ${id}.all.tsv ${id}.all.bw s3://bamcount-output/
    rm -rf /tmp/${id}
    echo $(cat $result)
}

#r=$(handler "$1")
#echo $r
