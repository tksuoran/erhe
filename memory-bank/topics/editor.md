§MBEL:5.0
©erhe::Topic::editor
@scope::Editor object lifetimes: part construction, scene-close + undo-removal reference rules, retention traps
@docs::doc/editor/coding_rules.md

[PATTERNS]
!rule::PartCtor¬ReadAppContext{nullptr-until-post-construct}→PassRefsExplicit
!scene-close-bug-class::parts-caching-scene-hosted-refs-must-handle-close{weak_ptr-insufficient:own-resolve-cache-pins-item}→PREFERRED:per-part-close_scene-subscription{part-drops-own-refs-by-get_item_host()-check;since-856dedd3:Brush_tool+Material_paint_tool+Material_preview+Brdf_slice+Physics_tool+Operations+Animation_player/window}||validate-App_scenes::is_host_registered(get_item_host())-on-access{precedent:Geometry_graph_window::resolve_target};rationale:parts→item-refs-invisible-from-item-side→push-must-reach-parts{virtual-Item::handle_item_host_update-rejected-for-this:wrong-direction;per-item-observers=R-phase-Asset_manager-userships}
!scene-close-watchdog::on_close_scene-only-QUEUES-scene{pending-shared_ptr};update_scene_close_leak_watches-ARMS-post-pump-in-tick{after-ALL-close_scene-subscribers,subscription-order-irrelevant}→60-frames→"scene-close leak:"-warn=bug{+"N holder(s)"=use_count-since-8df79fa1}|slot-pinned-items{Hotbar+Inventory-collect_pinned_items:brush+brush-material+material}→info-"intentionally pinned"{persistent-inventory-by-design}|asset-manager-pinned{Asset_manager::is_pinned:owned-strong-ref|declared-usership}→info-"intentionally pinned by the asset manager";clean="all N released (M intentionally pinned)"
!async-task-handle-retention::tf::AsyncTask-handle-keeps-task-node+callable+captures-alive-PAST-completion{taskflow-recycles-node-only-at-last-handle-release}→s_item_tasks{items.cpp}purged-per-frame{purge_completed_item_async_tasks@Editor::tick;8c3db108}¬only-on-next-submission;task-lambdas-capturing-scene_root/items=leak-if-handle-lingers
!scratch-retention-subclass::clear-at-point-of-USE-leaves-contents-pinned-BETWEEN-uses→item-shared_ptr-scratches-clear-AFTER-use-too{capacity-kept;8df79fa1:m_material_candidates+m_begin_selection_change_state+m_command_target_selection}
!self-sustaining-pin-subclass::part-re-resolves-weak{get_last_selected}into-own-strong-member-per-frame→member-IS-what-keeps-weak-lockable→never-expires{8df79fa1:Operations::m_make_mesh_config.material;Properties-pinned-m_target_items}
