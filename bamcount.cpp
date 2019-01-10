//
// Created by Ben Langmead on 2018-12-12.
// modified by cwilks 2019-01-09.
//

#include <iostream>
#include <vector>
#include <cassert>
#include <sstream>
#include <algorithm>
#include <string>
#include <cerrno>
#include <cstring>
#include <unordered_map>
#include <vector>
#include <htslib/sam.h>
#include <htslib/bgzf.h>
#include "bigWig.h"

//used for --annotation where we read a 3+ column BED file
static const int CHRM_COL=0;
static const int START_COL=1;
static const int END_COL=2;
//1MB per line should be more than enough for CIO
static const int LINE_BUFFER_LENGTH=1048576;
static const int BIGWIG_INIT_VAL = 17;

static const char USAGE[] = "BAM and BigWig utility.\n"
    "\n"
    "Usage:\n"
    "  bamcount <bam> [options]\n"
    "\n"
    "Options:\n"
    "  -h --help           Show this screen.\n"
    "  --version           Show version.\n"
    "  --read-ends         Print counts of read starts/ends\n"
    "  --coverage          Print per-base coverage (slow but totally worth it)\n"
    "   --bigwig            Output coverage as a BigWig file (requires libBigWig library)\n"
    "   --annotation        Path to BED file containing list of regions to sum coverage over (tab-delimited: chrm,start,end)\n"
    "  --alts              Print differing from ref per-base coverages\n"
    "   --include-softclip  Print a record for soft-clipped bases\n"
    "   --include-n         Print mismatch records when mismatched read base is N\n"
    "   --print-qual        Print quality values for mismatched bases\n"
    "   --delta             Print POS field as +/- delta from previous\n"
    "   --require-mdz       Quit with error unless MD:Z field exists everywhere it's\n"
    "                       expected\n"
    "  --no-head           Don't print sequence names and lengths in header\n"
    "  --echo-sam          Print a SAM record for each aligned read\n"
    "  --threads           # of threads to do BAM decompression\n"
    "  --double-count      Allow overlapping ends of PE read to count twice toward\n"
    "                      coverage\n"
    "\n";

static const char* get_positional_n(const char ** begin, const char ** end, size_t n) {
    size_t i = 0;
    for(const char **itr = begin; itr != end; itr++) {
        if((*itr)[0] != '-') {
            if(i++ == n) {
                return *itr;
            }
        }
    }
    return nullptr;
}

static bool has_option(const char** begin, const char** end, const std::string& option) {
    return std::find(begin, end, option) != end;
}

static const char** get_option(const char** begin, const char** end, const std::string& option) {
    const char** itr = std::find(begin, end, option);
    return ++itr;
}

/**
 * Holds an MDZ "operation"
 * op can be 
 */
struct MdzOp {
    char op;
    int run;
    char str[1024];
};

static inline std::ostream& seq_substring(std::ostream& os, const uint8_t *str, size_t off, size_t run) {
    for(size_t i = off; i < off + run; i++) {
        os << seq_nt16_str[bam_seqi(str, i)];
    }
    return os;
}

static inline std::ostream& kstring_out(std::ostream& os, const kstring_t *str) {
    for(size_t i = 0; i < str->l; i++) {
        os << str->s[i];
    }
    return os;
}

static inline std::ostream& cstr_substring(std::ostream& os, const uint8_t *str, size_t off, size_t run) {
    for(size_t i = off; i < off + run; i++) {
        os << (char)str[i];
    }
    return os;
}

/**
 * Parse given MD:Z extra field into a vector of MD:Z operations.
 */
static void parse_mdz(
        const uint8_t *mdz,
        std::vector<MdzOp>& ops)
{
    int i = 0;
    size_t mdz_len = strlen((char *)mdz);
    while(i < mdz_len) {
        if(isdigit(mdz[i])) {
            int run = 0;
            while(i < mdz_len && isdigit(mdz[i])) {
                run *= 10;
                run += (int)(mdz[i] - '0');
                i++;
            }
            if(run > 0) {
                ops.emplace_back(MdzOp{'=', run, ""});
                ops.back().str[0] = '\0';
            }
        } else if(isalpha(mdz[i])) {
            int st = i;
            while(i < mdz_len && isalpha(mdz[i])) i++;
            assert(i > st);
            ops.emplace_back(MdzOp{'X', i - st, ""});
            for(int j = 0; j < i ; j++) {
                ops.back().str[j] = mdz[st + j];
            }
            std::memcpy(ops.back().str, mdz + st, (size_t)(i - st));
            ops.back().str[i - st] = '\0';
        } else if(mdz[i] == '^') {
            i++;
            int st = i;
            while (i < mdz_len && isalpha(mdz[i])) i++;
            assert(i > st);
            ops.emplace_back(MdzOp{'^', i - st, ""});
            std::memcpy(ops.back().str, mdz + st, (size_t)(i - st));
            ops.back().str[i - st] = '\0';
        } else {
            std::stringstream ss;
            ss << "Unknown MD:Z operation: \"" << mdz[i] << "\"";
            throw std::runtime_error(ss.str());
        }
    }
}

#if 0
/**
 * Prints a stacked version of an alignment
 */
static void cigar_mdz_to_stacked(
        const BamAlignmentRecord& rec,
        vector<tuple<char, int, CharString>>& mdz,
        IupacString& rds,
        IupacString& rfs)
{
    const IupacString& seq{rec.seq};
    size_t mdz_off = 0, seq_off = 0;
    for (CigarElement<> e : rec.cigar) {
        const char &op = e.operation;
        bool ref_consuming = (strchr("DNMX=", op) != nullptr);
        if(ref_consuming && mdz_off >= mdz.size()) {
            stringstream ss;
            ss << "Found read-consuming CIGAR op after MD:Z had been exhausted" << endl;
            throw std::runtime_error(ss.str());
        }
        if (op == 'M' || op == 'X' || op == '=') {
            // Look for block matches and mismatches in MD:Z string
            size_t runleft = e.count;
            while (runleft > 0 && mdz_off < mdz.size()) {
                char mdz_op;
                size_t mdz_run;
                CharString mdz_str;
                std::tie(mdz_op, mdz_run, mdz_str) = mdz[mdz_off];
                size_t run_comb = std::min(runleft, mdz_run);
                runleft -= run_comb;
                assert(mdz_op == 'X' or mdz_op == '=');
                append(rds, infix(seq, seq_off, seq_off + run_comb));
                if (mdz_op == '=') {
                    append(rfs, infix(seq, seq_off, seq_off + run_comb));
                } else {
                    assert(length(mdz_str) == run_comb);
                    append(rfs, mdz_str);
                }
                seq_off += run_comb;
                if (run_comb < mdz_run) {
                    assert(mdz_op == '=');
                    get<1>(mdz[mdz_off]) -= run_comb;
                } else {
                    mdz_off++;
                }
            }
        } else if (op == 'I') {
            append(rds, infix(seq, seq_off, seq_off + e.count));
            for (size_t i = 0; i < e.count; i++) {
                append(rfs, '-');
            }
        } else if (op == 'D') {
            char mdz_op;
            size_t mdz_run;
            CharString mdz_str;
            std::tie(mdz_op, mdz_run, mdz_str) = mdz[mdz_off];
            assert(mdz_op == '^');
            assert(e.count == mdz_run);
            assert(length(mdz_str) == e.count);
            mdz_off++;
            for (size_t i = 0; i < e.count; i++) {
                append(rds, '-');
            }
            append(rfs, mdz_run);
        } else if (op == 'N') {
            for (size_t i = 0; i < e.count; i++) {
                append(rds, '-');
                append(rfs, '-');
            }
        } else if (op == 'S') {
            append(rds, infix(seq, seq_off, seq_off + e.count));
            for (size_t i = 0; i < e.count; i++) {
                append(rfs, '-');
            }
            seq_off += e.count;
        } else if (op == 'H') {
        } else if (op == 'P') {
        } else {
            stringstream ss;
            ss << "No such CIGAR operation as \"" << op << "\"";
            throw std::runtime_error(ss.str());
        }
    }
    assert(mdz_off == mdz.size());
}
#endif

static void output_from_cigar_mdz(
        const bam1_t *rec,
        std::vector<MdzOp>& mdz,
        bool print_qual = false,
        bool include_ss = false,
        bool include_n_mms = false,
        bool delta = false)
{
    uint8_t *seq = bam_get_seq(rec);
    uint8_t *qual = bam_get_qual(rec);
    // If QUAL field is *. this array is just a bunch of 255s
    uint32_t *cigar = bam_get_cigar(rec);
    size_t mdzi = 0, seq_off = 0;
    int32_t ref_off = rec->core.pos;
    for(int k = 0; k < rec->core.n_cigar; k++) {
        int op = bam_cigar_op(cigar[k]);
        int run = bam_cigar_oplen(cigar[k]);
        if((strchr("DNMX=", BAM_CIGAR_STR[op]) != nullptr) && mdzi >= mdz.size()) {
            std::stringstream ss;
            ss << "Found read-consuming CIGAR op after MD:Z had been exhausted" << std::endl;
            throw std::runtime_error(ss.str());
        }
        if(op == BAM_CMATCH || op == BAM_CDIFF || op == BAM_CEQUAL) {
            // Look for block matches and mismatches in MD:Z string
            int runleft = run;
            while(runleft > 0 && mdzi < mdz.size()) {
                int run_comb = std::min(runleft, mdz[mdzi].run);
                runleft -= run_comb;
                assert(mdz[mdzi].op == 'X' || mdz[mdzi].op == '=');
                if(mdz[mdzi].op == '=') {
                    // nop
                } else {
                    assert(mdz[mdzi].op == 'X');
                    assert(strlen(mdz[mdzi].str) == run_comb);
                    int cread = bam_seqi(seq, seq_off);
                    if(!include_n_mms && run_comb == 1 && cread == 'N') {
                        // skip
                    } else {
                        std::cout << rec->core.tid << ',' << ref_off << ",X,";
                        seq_substring(std::cout, seq, seq_off, (size_t)run_comb);
                        if(print_qual) {
                            std::cout << ',';
                            cstr_substring(std::cout, qual, seq_off, (size_t)run_comb);
                        }
                        std::cout << std::endl;
                    }
                }
                seq_off += run_comb;
                ref_off += run_comb;
                if(run_comb < mdz[mdzi].run) {
                    assert(mdz[mdzi].op == '=');
                    mdz[mdzi].run -= run_comb;
                } else {
                    mdzi++;
                }
            }
        } else if(op == BAM_CINS) {
            std::cout << rec->core.tid << ',' << ref_off << ",I,";
            seq_substring(std::cout, seq, seq_off, (size_t)run) << std::endl;
            seq_off += run;
        } else if(op == BAM_CSOFT_CLIP) {
            if(include_ss) {
                std::cout << rec->core.tid << ',' << ref_off << ",S,";
                seq_substring(std::cout, seq, seq_off, (size_t)run) << std::endl;
                seq_off += run;
            }
        } else if (op == BAM_CDEL) {
            assert(mdz[mdzi].op == '^');
            assert(run == mdz[mdzi].run);
            assert(strlen(mdz[mdzi].str) == run);
            mdzi++;
            std::cout << rec->core.tid << ',' << ref_off << ",D," << run << std::endl;
            ref_off += run;
        } else if (op == BAM_CREF_SKIP) {
            ref_off += run;
        } else if (op == BAM_CHARD_CLIP) {
        } else if (op == BAM_CPAD) {
        } else {
            std::stringstream ss;
            ss << "No such CIGAR operation as \"" << op << "\"";
            throw std::runtime_error(ss.str());
        }
    }
    assert(mdzi == mdz.size());
}

static void output_from_cigar(const bam1_t *rec) {
    uint8_t *seq = bam_get_seq(rec);
    uint32_t *cigar = bam_get_cigar(rec);
    uint32_t n_cigar = rec->core.n_cigar;
    if(n_cigar == 1) {
        return;
    }
    int32_t refpos = rec->core.pos;
    int32_t seqpos = 0;
    for(uint32_t k = 0; k < n_cigar; k++) {
        int op = bam_cigar_op(cigar[k]);
        int run = bam_cigar_oplen(cigar[k]);
        switch(op) {
            case BAM_CDEL: {
                std::cout << rec->core.tid << ',' << refpos << ",D," << run << std::endl;
                refpos += run;
                break;
            }
            case BAM_CSOFT_CLIP:
            case BAM_CINS: {
                std::cout << rec->core.tid << ',' << refpos << ',' << BAM_CIGAR_STR[op] << ',';
                seq_substring(std::cout, seq, (size_t)seqpos, (size_t)run) << std::endl;
                seqpos += run;
                break;
            }
            case BAM_CREF_SKIP: {
                refpos += run;
                break;
            }
            case BAM_CMATCH:
            case BAM_CDIFF:
            case BAM_CEQUAL: {
                seqpos += run;
                refpos += run;
                break;
            }
            case 'H':
            case 'P': { break; }
            default: {
                std::stringstream ss;
                ss << "No such CIGAR operation as \"" << op << "\"";
                throw std::runtime_error(ss.str());
            }
        }
    }
}

static void print_header(const bam_hdr_t * hdr) {
    for(int32_t i = 0; i < hdr->n_targets; i++) {
        std::cout << '@' << i << ','
                  << hdr->target_name[i] << ','
                  << hdr->target_len[i] << std::endl;
    }
}

static const long get_longest_target_size(const bam_hdr_t * hdr) {
    long max = 0;
    for(int32_t i = 0; i < hdr->n_targets; i++) {
        if(hdr->target_len[i] > max)
            max = hdr->target_len[i];
    }
    return max;
}

static void reset_array(uint32_t* arr, const long arr_sz) {
    for(long i = 0; i < arr_sz; i++)
        arr[i] = 0;
}

static void print_array(const char* prefix, 
                        char* chrm,
                        const uint32_t* arr, 
                        const long arr_sz,
                        const bool skip_zeros,
                        const bool collapse_regions,
                        bigWigFile_t* bwfp) {
    bool first = true;
    bool first_print = true;
    float running_value = 0;
    uint32_t last_pos = 0;
    //this will print the coordinates in base-0
    for(uint32_t i = 0; i < arr_sz; i++) {
        if(first || running_value != arr[i]) {
            if(!first) {
                if(running_value > 0 || !skip_zeros) {
                    if(bwfp && first_print)
                        bwAddIntervals(bwfp, &chrm, &last_pos, &i, &running_value, 1);
                    else if(bwfp)
                        bwAppendIntervals(bwfp, &last_pos, &i, &running_value, 1);
                    else
                        fprintf(stdout, "%s\t%u\t%u\t%.0f\n", prefix, last_pos, i, running_value);
                    first_print = false;
                }
            }
            first = false;
            running_value = arr[i];
            last_pos = i;
        }
    }
    if(!first)
        if(running_value > 0 || !skip_zeros)
            if(bwfp && first_print)
                bwAddIntervals(bwfp, &chrm, &last_pos, (uint32_t*) &arr_sz, &running_value, 1);
            else if(bwfp)
                bwAppendIntervals(bwfp, &last_pos, (uint32_t*) &arr_sz, &running_value, 1);
            else
                fprintf(stdout, "%s\t%u\t%lu\t%0.f\n", prefix, last_pos, arr_sz, running_value);
}

static const int32_t align_length(const bam1_t *rec) {
    //bam_endpos processes the whole cigar string
    return bam_endpos(rec) - rec->core.pos;
}

static const int32_t calculate_coverage(const bam1_t *rec, uint32_t* coverages, const bool double_count) {
    int32_t refpos = rec->core.pos;
    //lifted from htslib's bam_cigar2rlen(...) & bam_endpos(...)
    int32_t pos = refpos;
    int32_t algn_end_pos = refpos;
    const uint32_t* cigar = bam_get_cigar(rec);
    int k, z;
    //check for overlapping mate and corect double counting if exists
    int32_t mate_end = -1;
    char* qname = bam_get_qname(rec);

    for (k = 0; k < rec->core.n_cigar; ++k) {
        if(bam_cigar_type(bam_cigar_op(cigar[k]))&2) {
            int32_t len = bam_cigar_oplen(cigar[k]);
            for(z = algn_end_pos; z < algn_end_pos + len; z++)
                coverages[z]++;
            algn_end_pos += len;
        }
    }
    //fix paired mate overlap double counting
    if(!double_count && rec->core.tid == rec->core.mtid && (rec->core.flag & BAM_FPROPER_PAIR) == 2
            && algn_end_pos > rec->core.mpos && rec->core.pos < rec->core.mpos) {
        for(z = rec->core.mpos; z < algn_end_pos; z++)
            coverages[z]--;
    }
    return algn_end_pos;
}

typedef std::unordered_map<std::string, std::vector<long*>*> annotation_map_t;
//about 3x faster than the sstring/string::getline version
static const int process_region_line(char* line, const char* delim, annotation_map_t* amap) {
	char* line_copy = strdup(line);
	char* tok = strtok(line_copy, delim);
	int i = 0;
	char* chrm;
    long start;
    long end;
    int ret = 0;
	int last_col = END_COL;
	while(tok != NULL) {
		if(i > last_col)
			break;
		if(i == CHRM_COL) {
			chrm = strdup(tok);
		}
		if(i == START_COL)
			start = atol(tok);
		if(i == END_COL)
			end = atol(tok);
		i++;
		tok = strtok(NULL,delim);
	}
    long* coords = new long(2);
    coords[0] = start;
    coords[1] = end;
    if(amap->find(chrm) == amap->end()) {
        std::vector<long*>* v = new std::vector<long*>();
        (*amap)[chrm] = v;
    }
    (*amap)[chrm]->push_back(coords);
	if(line_copy)
		free(line_copy);
	if(line)
		free(line);
    return ret;
}
    
static const int read_annotation(FILE* fin, annotation_map_t* amap) {
	char* line = new char[LINE_BUFFER_LENGTH];
	size_t length = LINE_BUFFER_LENGTH;
	assert(fin);
	ssize_t bytes_read = getline(&line, &length, fin);
	int err;
	while(bytes_read != -1) {
	    err = process_region_line(strdup(line), "\t", amap);
        assert(err==0);
		bytes_read = getline(&line, &length, fin);
    }
	std::cerr << "building whole annotation region map done\n";
    return err;
}

static void sum_annotations(const uint32_t* coverages, const std::vector<long*>* annotations, const long chr_size, const char* chrm) {
    long z, j;
    for(z = 0; z < annotations->size(); z++) {
        long sum = 0;
        long start = (*annotations)[z][0];
        long end = (*annotations)[z][1];
        for(j = start; j < end; j++) {
            assert(j < chr_size);
            sum += coverages[j];
        }
        fprintf(stdout, "annot_sum\t%s\t%lu\t%lu\t%lu\n", chrm, start, end, sum);
    }
}

static void output_missing_annotations(const annotation_map_t* annotations, const std::unordered_map<std::string, bool>* annotations_seen) {
    for(auto itr = annotations->begin(); itr != annotations->end(); ++itr) {
        if(annotations_seen->find(itr->first) == annotations_seen->end()) {
            long z;
            std::vector<long*>* annotations_for_chr = itr->second;
            for(z = 0; z < annotations_for_chr->size(); z++) {
                long start = (*annotations_for_chr)[z][0];
                long end = (*annotations_for_chr)[z][1];
                fprintf(stdout, "annot_sum\t%s\t%lu\t%lu\t0\n", itr->first.c_str(), start, end);
            }
        }
    }
}
            
static bigWigFile_t* create_bigwig_file(const bam_hdr_t *hdr, const char* bam_fn) {
    if(bwInit(1<<BIGWIG_INIT_VAL) != 0) {
        fprintf(stderr, "Failed when calling bwInit with %d init val\n", BIGWIG_INIT_VAL);
        exit(-1);
    }
    char fn[1024] = "";
    sprintf(fn, "%s.bw", bam_fn);
    bigWigFile_t* bwfp = bwOpen(fn, NULL, "w");
    if(!bwfp) {
        fprintf(stderr, "Failed when attempting to open BigWig file %s for writing\n", fn);
        exit(-1);
    }
    //create with up to 10 zoom levels (though probably less in practice)
    bwCreateHdr(bwfp, 10);
    bwfp->cl = bwCreateChromList(hdr->target_name, hdr->target_len, hdr->n_targets);
    bwWriteHdr(bwfp);
    return bwfp;
}

int main(int argc, const char** argv) {
    const char** argv_ptr = argv;
    const char *bam_arg_c_str = get_positional_n(++argv_ptr, argv+argc, 0);
    if(!bam_arg_c_str) {
        std::cerr << "ERROR: Could not find <bam> positional arg" << std::endl;
        return 1;
    }
    std::string bam_arg{bam_arg_c_str};

    htsFile *bam_fh = sam_open(bam_arg.c_str(), "r");
    if(!bam_fh) {
        std::cerr << "ERROR: Could not open " << bam_arg << ": "
                  << std::strerror(errno) << std::endl;
        return 1;
    }
    //number of bam decompression threads
    //0 == 1 thread for the whole program, i.e.
    //decompression shares a single core with processing
    int nthreads = 0;
    if(has_option(argv, argv+argc, "--threads")) {
        const char** nthreads_ = get_option(argv, argv+argc, "--threads");
        nthreads = atoi(*nthreads_);
    }
    hts_set_threads(bam_fh, nthreads);

    bool print_qual = has_option(argv, argv+argc, "--print-qual");
    const bool include_ss = has_option(argv, argv+argc, "--include-softclip");
    const bool include_n_mms = has_option(argv, argv+argc, "--include-n");
    const bool double_count = has_option(argv, argv+argc, "--double-count");

    size_t recs = 0;
    bam_hdr_t *hdr = sam_hdr_read(bam_fh);
    if(!hdr) {
        std::cerr << "ERROR: Could not read header for " << bam_arg
                  << ": " << std::strerror(errno) << std::endl;
        return 1;
    }
    if(!has_option(argv, argv+argc, "--no-head")) {
        print_header(hdr);
    }
    std::vector<MdzOp> mdzbuf;
    bam1_t *rec = bam_init1();
    if (!rec) {
        std::cerr << "ERROR: Could not initialize BAM object: "
                  << std::strerror(errno) << std::endl;
        return 1;
    }
    kstring_t sambuf{ 0, 0, nullptr };
    bool first = true;
    //largest human chromosome is ~249M bases
    //long chr_size = 250000000;
    long chr_size = -1;
    uint32_t* coverages;
    uint8_t* annotation_tracking;
    bool compute_coverage = false;
    bool sum_annotation = false;
    bigWigFile_t *bwfp = NULL;
    annotation_map_t annotations; 
    std::unordered_map<std::string, bool> annotation_chrs_seen;
    if(has_option(argv, argv+argc, "--coverage")) {
        compute_coverage = true;
        chr_size = get_longest_target_size(hdr);
        coverages = new uint32_t[chr_size];
        if(has_option(argv, argv+argc, "--bigwig"))
            bwfp = create_bigwig_file(hdr, bam_arg.c_str());
        //setup hashmap to store BED file of *non-overlapping* annotated intervals to sum coverage across
        //maps chromosome to vector of uint arrays storing start/end of annotated intervals
        int err = 0;
        if(has_option(argv, argv+argc, "--annotation")) {
            sum_annotation = true;
            annotation_tracking = new uint8_t[chr_size];
            const char* afile = *(get_option(argv, argv+argc, "--annotation"));
            FILE* afp = fopen(afile, "r");
            err = read_annotation(afp, &annotations);
            fclose(afp);
            assert(annotations.size() > 0);
            std::cerr << annotations.size() << " chromosomes for annotated regions read\n";
        }
        assert(err == 0);
    }
    char prefix[50]="";
    int32_t ptid = -1;
    uint32_t* starts;
    uint32_t* ends;
    bool compute_ends = false;
    if(has_option(argv, argv+argc, "--read-ends")) {
        compute_ends = true;
        if(chr_size == -1) 
            chr_size = get_longest_target_size(hdr);
        starts = new uint32_t[chr_size];
        ends = new uint32_t[chr_size];
    }
    const bool echo_sam = has_option(argv, argv+argc, "--echo-sam");
    const bool compute_alts = has_option(argv, argv+argc, "--alts");
    const bool require_mdz = has_option(argv, argv+argc, "--require-mdz");
    while(sam_read1(bam_fh, hdr, rec) >= 0) {
        recs++;
        bam1_core_t *c = &rec->core;
        if((c->flag & BAM_FUNMAP) == 0) {
            //TODO track fragment lengths

            int32_t tid = rec->core.tid;
            int32_t end_refpos = -1;
            //track coverage
            if(compute_coverage) {
                if(tid != ptid) {
                    if(ptid != -1) {
                        sprintf(prefix, "cov\t%d", ptid);
                        print_array(prefix, hdr->target_name[ptid], coverages, hdr->target_len[ptid], false, true, bwfp);
                        //if we also want to sum coverage across a user supplied file of annotated regions
                        if(sum_annotation && annotations.find(hdr->target_name[ptid]) != annotations.end()) {
                            sum_annotations(coverages, annotations[hdr->target_name[ptid]], hdr->target_len[ptid], hdr->target_name[ptid]);
                            annotation_chrs_seen[hdr->target_name[ptid]] = true;
                        }
                    }
                    reset_array(coverages, chr_size);
                }
                end_refpos = calculate_coverage(rec, coverages, double_count);
            }

            //track read starts/ends
            if(compute_ends) {
                int32_t refpos = rec->core.pos;
                if(tid != ptid) {
                    if(ptid != -1) {
                        sprintf(prefix, "start\t%d", ptid);
                        print_array(prefix, hdr->target_name[ptid], starts, hdr->target_len[ptid], true, true, NULL);
                        sprintf(prefix, "end\t%d", ptid);
                        print_array(prefix, hdr->target_name[ptid], ends, hdr->target_len[ptid], true, true, NULL);
                    }
                    reset_array(starts, chr_size);
                    reset_array(ends, chr_size);
                }
                starts[refpos]++;
                if(end_refpos == -1)
                    end_refpos = refpos + align_length(rec) - 1;
                ends[end_refpos]++;
            }
            ptid = tid;

            //echo back the sam record
            if(echo_sam) {
                int ret = sam_format1(hdr, rec, &sambuf);
                if(ret < 0) {
                    std::cerr << "Could not format SAM record: " << std::strerror(errno) << std::endl;
                    return 1;
                }
                kstring_out(std::cout, &sambuf);
                std::cout << std::endl;
            }
            //track alt. base coverages
            if(compute_alts) {
                if(first) {
                    if(print_qual) {
                        uint8_t *qual = bam_get_qual(rec);
                        if(qual[0] == 255) {
                            std::cerr << "WARNING: --print-qual specified but quality strings don't seem to be present" << std::endl;
                            print_qual = false;
                        }
                    }
                    first = false;
                }
                const uint8_t *mdz = bam_aux_get(rec, "MD");
                if(!mdz) {
                    if(require_mdz) {
                        std::stringstream ss;
                        ss << "No MD:Z extra field for aligned read \"" << hdr->target_name[c->tid] << "\"";
                        throw std::runtime_error(ss.str());
                    }
                    output_from_cigar(rec); // just use CIGAR
                } else {
                    mdzbuf.clear();
                    parse_mdz(mdz + 1, mdzbuf); // skip type character at beginning
                    output_from_cigar_mdz(
                            rec, mdzbuf, print_qual,
                            include_ss, include_n_mms); // use CIGAR and MD:Z
                }
            }
        }
    }
    if(compute_coverage) {
        if(ptid != -1) {
            sprintf(prefix, "cov\t%d", ptid);
            print_array(prefix, hdr->target_name[ptid], coverages, hdr->target_len[ptid], false, true, bwfp);
            if(sum_annotation && annotations.find(hdr->target_name[ptid]) != annotations.end()) {
                sum_annotations(coverages, annotations[hdr->target_name[ptid]], hdr->target_len[ptid], hdr->target_name[ptid]);
                annotation_chrs_seen[hdr->target_name[ptid]] = true;
            }
        }
        delete coverages;
        if(sum_annotation)
            output_missing_annotations(&annotations, &annotation_chrs_seen);

    }
    if(compute_ends) {
        if(ptid != -1) {
            sprintf(prefix, "start\t%d", ptid);
            print_array(prefix, hdr->target_name[ptid], starts, hdr->target_len[ptid], true, true, NULL);
            sprintf(prefix, "end\t%d", ptid);
            print_array(prefix, hdr->target_name[ptid], ends, hdr->target_len[ptid], true, true, NULL);
        }
        delete starts;
        delete ends;
    }
    if(bwfp) {
        bwClose(bwfp);
        bwCleanup();
    }
    std::cout << "Read " << recs << " records" << std::endl;
    return 0;
}
