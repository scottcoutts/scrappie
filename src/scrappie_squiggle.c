#define _POSIX_SOURCE 1
#include <math.h>
#include <stdio.h>
#include <strings.h>
#include <sys/types.h>
#include <unistd.h>

#include "kseq.h"
#include "networks.h"
#include "scrappie_common.h"
#include "scrappie_licence.h"
#include "scrappie_stdlib.h"

KSEQ_INIT(int, read)

// Doesn't play nice with other headers, include last
#include <argp.h>


extern const char *argp_program_version;
extern const char *argp_program_bug_address;
static char doc[] = "Scrappie squiggler";
static char args_doc[] = "fasta [fasta ...]";
static struct argp_option options[] = {
    {"limit", 'l', "nreads", 0,
     "Maximum number of reads to call (0 is unlimited)"},
    {"output", 'o', "filename", 0, "Write to file rather than stdout"},
    {"prefix", 'p', "string", 0, "Prefix to append to name of each read"},
    {"rescale", 1, 0, 0, "Rescale network output"},
    {"no-rescale", 2, 0, OPTION_ALIAS, "Don't rescale network output"},
    {"licence", 10, 0, 0, "Print licensing information"},
    {"license", 11, 0, OPTION_ALIAS, "Print licensing information"},
    {0}
};


struct arguments {
    int limit;
    FILE * output;
    char * prefix;
    bool rescale;
    char **files;
};

static struct arguments args = {
    .limit = 0,
    .output = NULL,
    .prefix = "",
    .rescale = true,
    .files = NULL
};

static error_t parse_arg(int key, char *arg, struct argp_state *state) {
    switch (key) {
        int ret = 0;
    case 'l':
        args.limit = atoi(arg);
        assert(args.limit > 0);
        break;
    case 'o':
        args.output = fopen(arg, "w");
        if(NULL == args.output){
            errx(EXIT_FAILURE, "Failed to open \"%s\" for output.", arg);
        }
        break;
    case 'p':
        args.prefix = arg;
        break;
    case 1:
        args.rescale = true;
        break;
    case 2:
        args.rescale = false;
        break;
    case 10:
    case 11:
        ret = fputs(scrappie_licence_text, stdout);
        exit((EOF != ret) ? EXIT_SUCCESS : EXIT_FAILURE);
        break;

    case ARGP_KEY_NO_ARGS:
        argp_usage(state);
        break;

    case ARGP_KEY_ARG:
        args.files = &state->argv[state->next - 1];
        state->next = state->argc;
        break;

    default:
        return ARGP_ERR_UNKNOWN;
    }
    return 0;
}

static struct argp argp = { options, parse_arg, args_doc, doc };


int base_to_int(char c, bool allow_lower){
    c = allow_lower ? toupper(c) : c;
    switch(c){
        case 'A': return 0;
        case 'C': return 1;
        case 'G': return 2;
        case 'T': return 3;
        default:
            warnx("Unrecognised base %d in read", c);
    }
    return -1;
}


int * encode_bases_to_integers(char const * seq, size_t n){
    int * iseq = calloc(n, sizeof(int));
    for(size_t i=0 ; i < n ; i++){
        int ib = base_to_int(seq[i], true);
        iseq[i] = ib;
        if(-1 == ib){
            free(iseq);
            iseq = NULL;
            break;
        }
    }

    return iseq;
}


scrappie_matrix sequence_to_squiggle(char const * base_seq, size_t n, bool rescale){
    RETURN_NULL_IF(NULL == base_seq, NULL);

    int * sequence = encode_bases_to_integers(base_seq, n);
    RETURN_NULL_IF(NULL == sequence, NULL);

    scrappie_matrix squiggle = dna_squiggle(sequence, n, rescale);
    free(sequence);

    return squiggle;
}


int main_squiggle(int argc, char *argv[]) {
    argp_parse(&argp, argc, argv, 0, 0, NULL);
    if(NULL == args.output){
        args.output = stdout;
    }


    int nfile = 0;
    for (; args.files[nfile]; nfile++) ;

    int reads_started = 0;
    const int reads_limit = args.limit;

    for (int fn = 0; fn < nfile; fn++) {
        if (reads_limit > 0 && reads_started >= reads_limit) {
            break;
        }
        //  open file f
        FILE * fh = fopen(args.files[fn], "r");
        if(NULL == fh){
            warnx("Failed to open \"%s\" for input.\n", args.files[fn]);
            continue;
        }

        kseq_t * seq = kseq_init(fileno(fh));
        while(kseq_read(seq) >= 0){
            if (reads_limit > 0 && reads_started >= reads_limit) {
                break;
            }
            reads_started += 1;

            scrappie_matrix squiggle = sequence_to_squiggle(seq->seq.s, seq->seq.l, args.rescale);
            if(NULL != squiggle){
                fprintf(args.output, "#%s\n", seq->name.s);
                for(size_t i=0 ; i < squiggle->nc ; i++){
                    const size_t offset = i * squiggle->nrq * 4;
                    fprintf(args.output, "%zu\t%c\t%3.6f\t%3.6f\t%3.6f\n", i, seq->seq.s[i],
                            squiggle->data.f[offset + 0],
                            squiggle->data.f[offset + 1],
                            squiggle->data.f[offset + 2]);
                }
                squiggle = free_scrappie_matrix(squiggle);
            }
        }

        kseq_destroy(seq);
        fclose(fh);
    }

    return EXIT_SUCCESS;
}
