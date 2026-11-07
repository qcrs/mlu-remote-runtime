# Driver module memory query family

The family probe passed Native, Remote, LD_PRELOAD, isolated-client, typed
protocol, typed client fault, and metadata validation entries. Driver output is
identical in all modes:

```text
driver module memory usage path=110592 fatbin=110592
PASS API families
```

The test also rejects a missing path and an eight-byte malformed image. The
`core-smoke/` child directory records the application-level L2 run. L0 logs,
the generated artifact manifest, and API delta are kept beside these outputs.
