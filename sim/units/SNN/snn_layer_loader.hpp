#ifndef SNN_LAYER_LOADER_HPP
#define SNN_LAYER_LOADER_HPP

/*
 * snn_layer_loader.hpp
 *
 * Loads per-neuron NeuronParams from a CSV file into a std::vector<NeuronParams>.
 *
 * CSV format (header row required, then one row per neuron):
 *
 *   vth,vreset,alpha_leak,alpha_th,delta_th,refrac_period
 *   1.0,0.0,0.9492,0.98,1.25,8
 *   1.0,0.0,0.9200,0.97,1.30,6
 *   ...
 *
 * All voltage/multiplier columns are doubles; they are converted to Q8.8
 * via q88_from_double() at load time.
 * refrac_period is a plain unsigned integer (0-255).
 *
 * Row count determines the number of neurons; no separate size parameter.
 *
 * Errors: on any parse failure the function returns false and the output
 * vector is left in a valid but unspecified state. An error message is
 * written to stderr.
 *
 * Dependencies: q88.hpp, <cstdio>, <cstring>, <cstdlib>, <vector>
 * Compiler: GCC 6.3.0, -std=c++14
 */

#include "lif_neuron.hpp"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <vector>

/* Maximum characters per CSV line (generous for safety). */
static const int SNN_LOADER_LINE_MAX = 256;

/*
 * Trim leading/trailing whitespace and optional surrounding quotes from a
 * field string in-place. Returns pointer to the (possibly advanced) start.
 */
static char* loader_trim(char* s)
{
    /* trim leading whitespace */
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') ++s;
    /* trim trailing whitespace */
    char* end = s + strlen(s);
    while (end > s && (end[-1] == ' ' || end[-1] == '\t' ||
                       end[-1] == '\r' || end[-1] == '\n')) {
        --end;
        *end = '\0';
    }
    return s;
}

/*
 * Split a CSV line into fields, writing pointers into out_fields[].
 * Returns the number of fields found. Modifies line in-place.
 * Does NOT handle quoted fields containing commas — not needed here.
 */
static int loader_split(char* line, char** out_fields, int max_fields)
{
    int n = 0;
    char* p = line;
    while (n < max_fields) {
        out_fields[n++] = p;
        char* comma = strchr(p, ',');
        if (!comma) break;
        *comma = '\0';
        p = comma + 1;
    }
    return n;
}

/*
 * Column index constants — must match the header order.
 */
static const int COL_VTH          = 0;
static const int COL_VRESET       = 1;
static const int COL_ALPHA_LEAK   = 2;
static const int COL_ALPHA_TH     = 3;
static const int COL_DELTA_TH     = 4;
static const int COL_REFRAC       = 5;
static const int COL_COUNT        = 6;

/*
 * load_layer_params()
 *
 * Opens `filepath`, skips the header row, and parses each subsequent row
 * into a NeuronParams entry appended to `out`.
 *
 * Returns true on success (at least one neuron loaded).
 * Returns false on file-open failure, header mismatch, or parse error.
 */
static bool load_layer_params(const char* filepath,
                               std::vector<NeuronParams>& out)
{
    FILE* f = fopen(filepath, "r");
    if (!f) {
        fprintf(stderr, "[snn_layer_loader] Cannot open '%s'\n", filepath);
        return false;
    }

    char line[SNN_LOADER_LINE_MAX];
    int  row = 0;

    while (fgets(line, sizeof(line), f)) {
        /* Trim the whole line first */
        char* trimmed = loader_trim(line);
        if (*trimmed == '\0') continue;   /* skip blank lines */

        char* fields[COL_COUNT + 2];      /* +2 for safety */
        int   nf = loader_split(trimmed, fields, COL_COUNT + 2);

        if (row == 0) {
            /* Header row — verify column names (case-insensitive first char
             * check is sufficient; the format is well-defined). */
            if (nf < COL_COUNT) {
                fprintf(stderr,
                    "[snn_layer_loader] Header has %d columns, need %d\n",
                    nf, COL_COUNT);
                fclose(f);
                return false;
            }
            /* Light check: first column should start with 'v' or 'V' */
            char* h0 = loader_trim(fields[0]);
            if (h0[0] != 'v' && h0[0] != 'V') {
                fprintf(stderr,
                    "[snn_layer_loader] Unexpected first column '%s' "
                    "(expected 'vth')\n", h0);
                fclose(f);
                return false;
            }
            ++row;
            continue;
        }

        /* Data row */
        if (nf < COL_COUNT) {
            fprintf(stderr,
                "[snn_layer_loader] Row %d has %d fields, need %d — skipped\n",
                row, nf, COL_COUNT);
            ++row;
            continue;
        }

        NeuronParams p;
        p.vth         = q88_from_double(atof(loader_trim(fields[COL_VTH])));
        p.vreset      = q88_from_double(atof(loader_trim(fields[COL_VRESET])));
        p.alpha_leak  = q88_from_double(atof(loader_trim(fields[COL_ALPHA_LEAK])));
        p.alpha_th    = q88_from_double(atof(loader_trim(fields[COL_ALPHA_TH])));
        p.delta_th    = q88_from_double(atof(loader_trim(fields[COL_DELTA_TH])));

        long refrac = atol(loader_trim(fields[COL_REFRAC]));
        if (refrac < 0 || refrac > 255) {
            fprintf(stderr,
                "[snn_layer_loader] Row %d: refrac_period %ld out of range "
                "[0,255]\n", row, refrac);
            fclose(f);
            return false;
        }
        p.refrac_period = (uint8_t)refrac;

        out.push_back(p);
        ++row;
    }

    fclose(f);

    if (out.empty()) {
        fprintf(stderr,
            "[snn_layer_loader] '%s' contains no neuron rows\n", filepath);
        return false;
    }

    return true;
}

#endif /* SNN_LAYER_LOADER_HPP */