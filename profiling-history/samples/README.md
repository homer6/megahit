# Profiling samples

Characterization of the data samples used for profiling. **Profiling is highly sample-dependent** —
runtime, IPC, cache behavior, and the per-stage balance all shift with organism mix, read length,
coverage, and GC. **Only compare runs that used the same sample and subsample.** Each run record links
the sample doc it used; each sample doc lists the runs that used it.

Raw FASTQ lives in the gitignored `profiling/data/`; these docs (source, characteristics, checksums,
subsample derivations) are committed.

| Sample | Organism | Platform | Read len | Reads | GC | Size (gz) | Doc |
|---|---|---|---|---|---|---|---|
| SRR341725 | human gut metagenome (Qin 2012 T2D) | Illumina GA II | 90 bp | 12.74M pairs / 2.29 Gbp | ~43% | ~1.8 GB | [SRR341725.md](SRR341725.md) |

## Adding a sample

1. Download to `profiling/data/` (gitignored) and **verify md5 against ENA**.
2. Create `samples/<accession>.md` (provenance + characteristics + checksums + subsample derivation). Pull
   metadata from the ENA filereport API:
   `curl "https://www.ebi.ac.uk/ena/portal/api/filereport?accession=<ACC>&result=read_run&fields=run_accession,study_accession,sample_accession,scientific_name,instrument_model,library_strategy,library_layout,read_count,base_count,fastq_md5,fastq_ftp&format=tsv"`
3. Measure read length + GC locally (don't trust assumptions); record exact subsample commands + md5.
4. Add a row above and link the runs that use it.
