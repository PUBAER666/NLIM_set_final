# Event failure triage

1. Confirm valid sample IDs, epoch continuity, rail flags and no missing blocks. A link being connected does not establish data validity.
2. If extraction stops at SOURCE_UNSTABLE, record each pre/post window's power, standard deviation and threshold. Frozen bad prehistory cannot improve by repeatedly calculating later post windows. Never diagnose this as insufficient registered samples before reaching the matcher.
3. If extraction completes and the base model returns unknown, inspect the registered candidate, support distance, acceptance limit and class separation. An auxiliary classifier rejecting the base classes and a registration matcher rejecting a new identity are distinct decisions.
4. If the name is correct but energy remains zero, inspect the accounting candidate, expected/measured power, consecutive count, reset cause and timeout. Do not silently invent energy for uncertain intervals.
5. On retries, clear invalid old scores. Pending means unavailable, not zero probability. Group repeated unassigned-power notices by episode rather than presenting every transient as a new unknown appliance.

Source observations: repeated ON failures with stable OFF matches; 13-of-25 recovery count reset; stale auxiliary values in pending windows. The supplied old logs do not identify every connector operation or contain every raw sample, so they cannot prove the precise physical cause.
