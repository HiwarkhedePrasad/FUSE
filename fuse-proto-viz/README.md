# fuse-proto-viz

MVP scaffold for the FUSE protocol interceptor, decoder, and visualizer pipeline.

## Layout

- `src/interceptor/`: proxy loop and shared-memory plumbing
- `src/decoder/`: protocol decode and session tracking
- `src/visualizer/`: Flask app and sequence diagram assets
- `include/`: shared headers
- `tests/`: unit tests for core components
- `docs/`: protocol and architecture notes
