# Performance evidence

Use latency = clock cycles / clock frequency. Confirm clock units and which transaction completed. Exclude interface-rejection requests from valid-inference averages. Simulator wall-clock runtime measures the computer running the simulator, not accelerator latency.

State whether the timer includes preprocessing, transfers, cache operations and postprocessing. Four parallel lanes at II=1 imply four times the ideal inner-loop throughput of a same-clock one-lane II=1 structure; they do not prove four times a CPU's speed. Interleaving eight accumulations hides dependencies and does not automatically multiply throughput by eight again.

Keep three categories separate: analytical structure comparison, synthesis/simulation estimates, and board measurements. A model-call probability is not its share of total execution time. Report utilization of the complete design separately from HLS IP estimates. If hardware or clock changes, existing timing and resource evidence no longer verifies the changed build.
