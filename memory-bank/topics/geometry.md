§MBEL:5.0
©erhe::Topic::geometry
@scope::Geometry / geogram threading, geometry graph payload and pin rules
@docs::doc/erhe/geogram.md+doc/erhe/geometry.md+doc/editor/geometry_nodes.md

[PATTERNS]
!geogram-threading::PDEL/parallel-algos-require-no-other-geogram-threads{unenforceable-in-erhe→use-sequential-BDEL-for-convex-hull;asserts→ASSERT_THROW-sans-debugger;upstream-ask:doc/erhe/geogram.md}
!geometry-payload-invariant::graph-payload-geometries-carry-connectivity+edges{process_for_graph}→every-producer-incl-merges-must-process{violation=fastfail-on-worker,uncatchable}
GraphPins::input-default-single-link{editors-replace-on-connect,one-Compound-undo}|Pin::multi_link=multi-input-socket{accumulate;join+instance-points+realize-instances}
GraphSourceNodes::external-item-refs{brush/scene_mesh}capture-geometry-MAIN-thread{lazy-getters-worker-unsafe}+serialize-by-name{owner-scene→all-scenes-resolution;shadow-clones-ownerless}
