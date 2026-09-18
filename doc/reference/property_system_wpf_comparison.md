# erhe property system vs WPF dependency properties

A feature-by-feature comparison of `erhe::property` against the WPF
property system it was ported from. Companion to `doc/property_system.md`
(design record) and `src/erhe/property/notes.md` (library reference); this
document adds nothing to either, it only lines them up against WPF.

Sources compared:

- erhe: `doc/property_system.md`, `src/erhe/property/notes.md` and the
  headers under `src/erhe/property/erhe_property/`.
- WPF: `https://github.com/dotnet/wpf` at commit
  `1cfc37f708f91ff4556bd25af414546c446f3a16` (2026-08-21), files under
  `src/Microsoft.DotNet.Wpf/src/`:
  - `WindowsBase/System/Windows/`: `DependencyProperty.cs`,
    `PropertyMetadata.cs`, `DependencyObject.cs`, `EffectiveValueEntry.cs`,
    `DependencyPropertyKey.cs`, `Expression.cs`, `DependentList.cs`,
    `Freezable.cs`, `LocalValueEnumerator.cs`, `DependencyObjectType.cs`,
    `Threading/DispatcherObject.cs`, `UncommonField.cs`,
    `DeferredReference.cs`, `MS/Internal/DefaultValueFactory.cs`.
  - `PresentationCore/System/Windows/Media/Animation/AnimationStorage.cs`,
    `IAnimatable` (`UIElement`, `Animatable`).
  - `PresentationFramework/System/Windows/`: `FrameworkPropertyMetadata.cs`,
    `TreeWalkHelper.cs`, `DependencyPropertyHelper.cs`, `Style.cs`,
    `Trigger.cs`, `PropertyPath.cs`, `Data/Binding*.cs`,
    `Data/BindingOperations.cs`, `Data/MultiBinding.cs`,
    `Data/PriorityBinding.cs`.

Terminology: "WPF core" means `WindowsBase` (`DependencyObject` and
friends), "WPF framework" means `PresentationFramework`
(`FrameworkElement`, styles, bindings, inheritance tree walks). erhe folds
the parts it took from both into one library plus the `Item_base`
integration and the editor.

## 1. Architecture at a glance

| Aspect | WPF | erhe |
|---|---|---|
| Layering | Three assemblies: core store (`WindowsBase`), animation and visuals (`PresentationCore`), tree / style / binding (`PresentationFramework`) | One library `erhe::property` (store, inheritance hooks, expressions, styles, sealing) + `erhe::item` (`Item_base : Dependency_object`, `Hierarchy` tree) + editor (undo, rows, MCP) |
| Object base | `DependencyObject : DispatcherObject` (thread-affine: owned by one thread for life, see 2.10) | `Dependency_object` (no owning thread; any thread may use it while holding the item host's mutex, see 2.10) |
| Value type | Boxed `object`, any CLR type, checked by `IsValidType` | `std::variant` of 12 fixed alternatives (`Property_value`) plus C++ enumerations through `Enum_value` |
| Identity of a property | `DependencyProperty` with `GlobalIndex`, keyed by (name, owner CLR `Type`) | `Dependency_property` with `uint16_t` index, keyed by (name, owner type id) |
| Per-type metadata | Keyed by `DependencyObjectType` (CLR class hierarchy), inherited down the class chain, merged | Keyed by owner type id; nearest ancestor on the id chain wins, else default; replaced, not merged |
| Per-object store | `EffectiveValueEntry[]` sorted by `GlobalIndex`, binary search; also stores `UncommonField` slots | `std::vector<Effective_value_entry>` sorted by index, binary search; properties only |
| Authored text form | XAML, `TypeConverter`, `ValueSerializer` | `to_string` / `parse_value` (`property_string.hpp`), glTF extras, MCP strings |

## 2. Feature mapping

Status legend: **ported** (same concept, same semantics), **adapted**
(same concept, different mechanism or narrower scope), **omitted**
(deliberately left out, `doc/property_system.md` section 5), **future**
(listed in section 6 of the design record), **erhe-only** (no WPF
counterpart).

### 2.1 Registration and metadata

| WPF | erhe | Status | Notes |
|---|---|---|---|
| `DependencyProperty.Register(name, type, ownerType, metadata, validate)` | `Property<T>::register_property(name, owner_type, metadata)` (+ validate overloads) | ported | Static-member registration in both. erhe also registers from a single-threaded startup window (`register_texture_graph_properties`). |
| `RegisterReadOnly` -> `DependencyPropertyKey` | `Property_key<T>::register_read_only` | ported | Same key-as-write-permission model. Nothing in erhe registers one yet; read-only candidates became computed properties (D26). |
| `RegisterAttached`, `RegisterAttachedReadOnly` | `Property<T>::register_attached` | adapted | Qualified `<Owner>.<name>` lookup, D12 listing rule (`visible_when` or a local value), the Properties window's Add Property picker and Remove Property, MCP listing by qualified name (D3, D12, D13); first user the layout hints (section 4.14). `AttachedPropertyBrowsable*` attributes omitted: the registering type's `visible_when` plays that role. |
| `AddOwner(ownerType, metadata)` | `Dependency_property::add_owner(owner_type, metadata)` | ported | erhe registers the (owner, name) alias in the registry like WPF. |
| `OverrideMetadata(forType, metadata[, key])` | `override_metadata(owner_type, metadata)` | adapted | WPF `Merge`: unset default falls back to base, `PropertyChangedCallback` delegates are *chained* (base first), coerce replaced. erhe: unset default falls back to base, everything else *replaces*; no callback chaining. WPF seals metadata once used (`TypeMetadataCannotChangeAfterUse`); erhe relies on the startup write window. |
| Unique (name, owner) enforced (`PropertyAlreadyRegistered`) | `ERHE_FATAL` on duplicate (owner type, name) | ported | |
| `DependencyProperty.FromName(name, ownerType)` | `Property_registry::find(owner, name)`, `find_for_object(object_type, name)` | ported | `find_for_object` walks the owner type chain nearest first, the same base-class walk WPF does through `DependencyObjectType`. |
| `GlobalIndex`, `RegisteredPropertyList` | `get_index()`, `Property_registry::get(index)` / `get_count()` | ported | |
| Owner identity = CLR class (`DependencyObjectType`, derived shadows base) | `Owner_type` id with a parent link (D27); `Item<>` allocates one per class under its C++ parent | ported | Ids are run-local, never serialized. `Item_type` bits stay outside the registry. |
| `PropertyMetadata.DefaultValue` (any object, `DefaultValueFactory` for mutable / `Freezable` defaults, per-instance cached) | `Property_metadata::default_value` (`Property_value`) | adapted | All erhe values are value types, so no factory or per-instance default cache is needed. `DefaultValueMayNotBeUnset` / `MayNotBeExpression` checks map to type + validate verification at registration. |
| `PropertyChangedCallback(d, args)` | `Property_changed_callback(Dependency_object&, Property_changed_args&)` | ported | Same shape. |
| `CoerceValueCallback(d, baseValue)` | `Coerce_callback(const Dependency_object&, const Property_value&)` | ported | Same shape; see 2.3 for when it runs. |
| `ValidateValueCallback(value)` (no object) | `Validate_callback(const Property_value&)` | ported | Both value-only. WPF throws `ArgumentException`; erhe logs and drops the write (D7). |
| `FrameworkPropertyMetadataOptions` (`AffectsMeasure`, `AffectsArrange`, `AffectsRender`, `Inherits`, `NotDataBindable`, `BindsTwoWayByDefault`, `Journal`, ...) | `Property_flags` (`affects_transform`, `affects_draw_list_partition`, `affects_shader_variant`, `serialize`) + `Property_metadata::inherits` | adapted | Same idea (consequence flags read by the framework layer), scene-specific set. In WPF `FrameworkElement.OnPropertyChanged` acts on the flags; in erhe only the editor hook `App_context::on_item_property_changed` does (the library never acts on them). |
| `UIPropertyMetadata.IsAnimationProhibited` | none | future | Waits on the animated layer. |
| Designer / editor metadata (`Category`, `Description`, `Browsable` attributes on the CLR property) | `Property_ui` (min / max / step, presentation, logarithmic, group, tooltip, label, developer-only, `visible_when`) | erhe-only | WPF keeps UI metadata outside the property system; erhe puts it in the metadata block because the Properties window is generic. |
| none | `Property_bridge` (property stored in the object's own member) | erhe-only | WPF has no equivalent; every DP value lives in the entry array. erhe uses it for `Node` TRS, `Camera` projection and graph node parameters (D18). |
| Read-only DP whose value the owner sets through the key | `Property_metadata::compute` (`register_computed`) | adapted | WPF stores the computed value (`SetValue(key, ...)`); erhe calls a provider on every read and never stores it (D26). |

### 2.2 Value precedence

WPF `BaseValueSourceInternal` (12 base sources) plus modifier bits in
`FullValueSource`; erhe `Value_source` (6 values). Highest precedence
first.

| WPF base source / modifier | erhe | Notes |
|---|---|---|
| (modifier) `IsCoerced` | `is_coerced()` | Both track coercion as a modifier on top of the base value. |
| (modifier) `IsCoercedWithCurrentValue` (`SetCurrentValue`) | `set_current_value` on an expression keeps the formula | WPF exposes it as `ValueSource.IsCurrent`; erhe has no separate source flag for it. |
| (modifier) `IsAnimated` | none | future: animated layer between coerced and local. |
| `Local` (value or expression; modifier `IsExpression`) | `local` / `expression` | erhe splits the expression case into its own `Value_source` value; WPF reports `Local` + `IsExpression`. |
| `ParentTemplateTrigger`, `ParentTemplate` | none | omitted (templates). |
| `ImplicitReference` | none | omitted (implicit styles / resources). |
| `StyleTrigger`, `TemplateTrigger` | none | omitted (triggers). |
| `Style` | `style` | ported (D25): one `Property_style` per object, setters only, no triggers, no `BasedOn`, no `TargetType`. |
| `ThemeStyleTrigger`, `ThemeStyle` | none | omitted. |
| `Inherited` | `inherited` | ported; caching differs, see 2.5. |
| `Default` | `default_value` | ported. |
| `Unknown` | none | |
| Read-only DP with `GetReadOnlyValueCallback` | `computed` | erhe reports computed as its own source that bypasses every layer. |

Public reporting: WPF `DependencyPropertyHelper.GetValueSource` returns a
`ValueSource` struct (`BaseValueSource` + `IsExpression` / `IsAnimated` /
`IsCoerced` / `IsCurrent`); erhe `get_value_source` + `is_coerced` +
`get_expression`.

### 2.3 Object API and write semantics

| WPF `DependencyObject` | erhe `Dependency_object` | Status | Notes |
|---|---|---|---|
| `GetValue(dp) -> object` | `get_value(Property<T>) -> T`, `get_value(const Dependency_property&) -> Property_value` | ported | erhe adds the typed handle path (R2). |
| `SetValue(dp, value)` | `set_value(property, value)` (typed and untyped; untyped returns `bool`) | ported | Both replace an installed expression with the value. |
| `SetValue(DependencyPropertyKey, value)` | `set_value(Property_key<T>, value)` | ported | |
| `SetCurrentValue(dp, value)` | `set_current_value(property, value)` | ported | Both keep the expression / binding and write the value on top. WPF also keeps it distinct from a local value for style / trigger precedence (a "current value" is replaced by a later lower-precedence change); erhe only defines it against an expression. |
| `ClearValue(dp)` / `ClearValue(key)` | `clear_value(property)` / `clear_value(key)` | ported | Both drop the local layer including an expression. |
| `CoerceValue(dp)` | `coerce_value(property)` | adapted | WPF re-runs coerce on whatever the base value is. erhe re-runs it only against a *local* value; a property without a local value is coerced on every read and has nothing to re-run. |
| `InvalidateProperty(dp)` (re-evaluate effective value from all sources) | `invalidate_dependents(property)` | adapted | Different purpose: WPF re-evaluates the property itself; erhe pushes "my storage changed outside `set_value`" to expressions that read it (for bridged and computed properties). erhe has no need for self-invalidation because nothing but the local layer is cached. |
| `ReadLocalValue(dp) -> object or UnsetValue` | `read_local_value(property) -> std::optional<T>` | ported | WPF returns the `Expression` object for a bound property; erhe returns the last evaluated value and exposes the formula through `get_expression` / `read_local_state`. |
| `GetLocalValueEnumerator()` | `for_each_local_value(callback)`, `Property_set::read_local_values` | ported | |
| `DependencyProperty.UnsetValue` sentinel | `std::optional` / `std::nullopt` | adapted | |
| `ShouldSerializeProperty(dp)` (virtual, default = has local value) | `Property_flags::serialize` + "has a non-bridged, non-expression local value" | adapted | WPF is a per-object virtual; erhe is per-property metadata read by the glTF writers. |
| `OnPropertyChanged(args)` (virtual; base runs the metadata callback) | `on_property_changed(args)` (virtual) | adapted | Order differs: WPF virtual runs first and *contains* the metadata callback; erhe runs metadata callback, then the virtual, then observers. |
| `DependencyPropertyChangedEventArgs` (`Property`, `OldValue`, `NewValue`, `Metadata`, `IsAValueChange`, `IsASubPropertyChange`, `OperationType`) | `Property_changed_args` (property, old / new value, old / new `Value_source`) | adapted | erhe adds the source transition; WPF adds sub-property changes (a mutable `Freezable` value changed inside) which erhe cannot have with value-type values. |
| `IsSealed` / `Seal()` (internal, one-way; for `Freezable` see 2.7) | `seal()` / `unseal()` / `is_sealed()` | adapted | See 2.7. |
| `UncommonField<T>` (extra per-object slots in the same entry array) | none | omitted | |
| `DeferredReference` (lazy resource values) | none | omitted | |
| `BooleanBoxes` helpers | n/a (variant) | | |

### 2.4 Change notification and observers

| WPF | erhe | Status | Notes |
|---|---|---|---|
| Notification fires only when the effective value changed (`IsAValueChange`) | Fires when the effective value *or its source* changed | adapted | erhe notifies on local -> inherited with an equal value; WPF only passes inheritance bookkeeping through with `IsAValueChange == false`. |
| Callback order: `OnPropertyChanged` virtual (which invokes `PropertyChangedCallback`) -> `DependentList.InvalidateDependents` (expressions) -> framework `FrameworkElement.OnPropertyChanged` (inheritance propagation, `Affects*` flags) | metadata `property_changed` -> virtual `on_property_changed` -> observers -> dependents (expressions) -> descendants (inheritance) | adapted | |
| No batching primitive (each `SetValue` notifies) | `Change_batch` RAII: queue, dedupe per property, deliver once with before / after of the whole batch | erhe-only | `Material::set_values`, `Property_set::apply` use it. WPF has `ISupportInitialize` on some classes but nothing in the property system itself. |
| `DependencyPropertyDescriptor.AddValueChanged(component, handler)` (per property, per object, via `TypeDescriptor`) | `add_observer(property, callback) -> Observer_token` | adapted | erhe token is RAII and is invalidated when the object dies; WPF handlers must be removed by hand (a well-known leak source). |
| none | `add_observer(callback)` for every property of the object | erhe-only | Saves one token per registered property for thumbnails and previews (D21). |
| `Freezable.Changed` event (any change on a freezable object, incl. sub-properties) | `add_observer(callback)` on any object | adapted | The closest WPF analogue, but only on `Freezable`. |
| `INotifyPropertyChanged` (CLR side, used by bindings) | none | omitted | erhe items are all `Dependency_object`s; no POCO source path. |
| Sub-property change (`IsASubPropertyChange`) | none | omitted | Values are immutable value types. |

### 2.5 Inheritance

| WPF | erhe | Status | Notes |
|---|---|---|---|
| `FrameworkPropertyMetadataOptions.Inherits` | `Property_metadata::inherits` | ported | |
| Inheritance parent: logical / visual tree (`FrameworkElement`), `InheritanceContext` for non-tree objects (`Freezable` in a property) | Two virtuals: `get_inheritance_parent()`, `for_each_inheritance_child()`; `Hierarchy` implements them, `Node` adds attachments, `Node_attachment` names its node | adapted | erhe's library knows no tree; the object supplies it (D8). |
| `InheritanceBehavior` (`SkipToAppNow`, `SkipToThemeNext`, ...) and `OverridesInheritanceBehavior` | none | omitted | |
| Inherited value **cached** in the child's entry (`BaseValueSourceInternal.Inherited`; `SynchronizeInheritanceParent`, `TreeWalkHelper.InvalidateOnInheritablePropertyChange`) | **Not cached**: a read walks up to the closest ancestor with a local or style value | adapted | erhe trades read cost for no cache invalidation state. |
| `TreeWalkHelper.InvalidateOnInheritablePropertyChange` (descendant invalidation stopping at a local value) | `propagate_to_descendants` from `set_value` / `clear_value` / `set_style` | ported | Same stopping rule; erhe also treats a style value as a stop. |
| `TreeWalkHelper.InvalidateOnTreeChange` (reparent notifications) | `capture_inheritance_snapshot` / `apply_inheritance_snapshot` around `Hierarchy::set_parent` and `Node_attachment::set_node` | adapted | erhe diffs a before / after snapshot so descendants see correct old values. |
| `IsVisible` = parent AND self (computed, read-only) | `visible` = closest ancestor with a local value wins (CSS `visibility` style) | adapted | Deliberate (D23): a child under a hidden parent can be shown with a local `true`. |
| Resource inheritance (`InvalidateOnResourcesChange`) | none | omitted | |

### 2.6 Expressions and bindings

| WPF | erhe | Status | Notes |
|---|---|---|---|
| `Expression` base (internal; `GetValue`, `SetValue`, `OnAttach` / `OnDetach`, `GetSources`, `OnPropertyInvalidation`, `Copy`) stored as the local value | `Expression` (`expression.hpp`) held in the entry next to the last evaluated value | adapted | erhe keeps the evaluated value in the entry so every read path stays unchanged. |
| `ExpressionMode` (`None`, `NonSharable`, `ForwardsInvalidations`, `SupportsUnboundSources`) | none | omitted | One expression per (object, property); unresolved references are the norm and resolve lazily. |
| `DependencySource[]` + `DependentList` (weak references, `InvalidateDependents` from `NotifyPropertyChange`) | Per-source dependent list of (target object, target property, source property), removed on replace / destroy | ported | erhe lists hold raw pointers cleaned up by destructor hooks, not weak references. |
| `Binding` (`Path`, `Source`, `ElementName`, `RelativeSource`, `Converter`, `ConverterParameter`, `StringFormat`, `FallbackValue`, `TargetNullValue`, `Delay`, `IsAsync`, `ValidationRules`, ...) | Reference syntax `{[object/]property[.x|.y|.z|.w]}` inside a tinyexpr formula | adapted | A binding is a one-reference formula (D22). Object resolution: `""` self, `..` parent, a name through `Item_host::find_hosted_item`. No converters (the formula is the converter), no fallback value (error text + last value instead), no async. |
| `BindingMode` (`TwoWay`, `OneWay`, `OneTime`, `OneWayToSource`, `Default`) and `UpdateSourceTrigger` | one-way only | omitted | Two-way is section 5 out of scope: ImGui windows write the item directly. |
| `MultiBinding` + `IMultiValueConverter` | Any formula with several references | adapted | The formula is the multi-value converter. |
| `PriorityBinding` | none | omitted | |
| `PropertyPath` (nested paths, indexers, attached-property syntax) | Single property name (+ component) on one object | adapted | No sub-object paths; a reference reaches one property of one item. |
| `BindingOperations.SetBinding` / `ClearBinding` / `GetBindingExpression` / `IsDataBound` | `set_expression` / `clear_value` / `get_expression` / `get_value_source() == expression` | ported | |
| Binding errors go to the trace / `Validation` system | `get_expression_error` (unresolved reference, type problem, `cycle`), red frame in the row | adapted | |
| Cycle handling: none at the property level (bindings guard re-entry per expression) | `set_expression` rejects self reference and walks the resolved graph; a lazily closed cycle is caught by a per-expression re-entry guard | erhe-only | |
| Sources can be any object with `INotifyPropertyChanged` or a DP | Sources are `Dependency_object` properties only (stored, bridged or computed) | adapted | |
| Push only (source change -> target) | Push from `deliver` / `invalidate_dependents` **and** pull (a read with unresolved references retries resolution) | erhe-only | Lets a loaded scene or a clone converge without a frame hook. |
| Expression copied via `Expression.Copy` when the object is cloned | Formula text copied unresolved; the copy resolves lazily | adapted | |
| Whole-value binding; component access needs a converter | comma-separated one-per-component formulas with broadcast | erhe-only | |

### 2.7 Sealing, freezing and copying

| WPF | erhe | Status | Notes |
|---|---|---|---|
| `Freezable.Freeze()` (one-way; makes the object immutable and thread-free; `CanFreeze`, `IsFrozen`) | `seal()` / `unseal()` | adapted | erhe's seal is reversible because it mirrors the user's `lock_edit` toggle (D24). Sealing does not change threading rules in erhe. |
| Frozen object: every write throws | Sealed object: local-layer writes (`set_value`, `set_current_value`, `clear_value`, `set_expression`, `apply_local_state`, `set_style`) rejected with a logged error and `false`; reads, inherited notifications, observers and installed expressions keep working | adapted | |
| `DependencyObject.Seal()` (internal; promotes cached values to local before sealing) | none | omitted | |
| `Freezable.Clone()` (deep, unfrozen, expressions copied) / `CloneCurrentValue()` (evaluated values) / `GetAsFrozen` / `GetCurrentValueAsFrozen` | Copy constructor / assignment copies entries (local values, coerced values, expression text) and the style pointer; not sealed; observers, batches, inherited state not copied | adapted | The `Clone` vs `CloneCurrentValue` pair maps to "copy" vs `Property_set::read_local_values` (which bakes expression results). |
| `Freezable.Changed` event; sub-property change propagation | see 2.4 | | |

### 2.8 Styles, templates and triggers

| WPF | erhe | Status | Notes |
|---|---|---|---|
| `Style` (`Setters`, `Triggers`, `Resources`, `TargetType`, `BasedOn`, sealed on first use) | `Property_style` (name + immutable `Property_set`), shared through `std::shared_ptr<const>` | adapted | Setters only. No `BasedOn` chain, no `TargetType` check (a style is applied per object; properties the type lacks are skipped by the bag semantics), no resources. |
| `FrameworkElement.Style` DP, implicit styles by type from resource dictionaries, theme styles | `set_style` / `get_style` per object, explicit only | adapted | |
| `Trigger`, `DataTrigger`, `MultiTrigger`, `EventTrigger` | none | omitted | A conditional value is expressed as a formula (`select`, `lt`, ...) on the property instead. |
| `ControlTemplate`, `DataTemplate`, `TemplateBinding` | none | omitted | |
| Style sealed after use (`Style.Seal`) | `Property_style` immutable after construction; a changed preset is a new object | ported | |
| Style serialized in XAML | Session state; glTF exports effective values | future | Needs a preset library that owns names. |

### 2.9 Animation

| WPF | erhe | Status | Notes |
|---|---|---|---|
| `IAnimatable.BeginAnimation` / `ApplyAnimationClock`, `AnimationStorage`, `IsAnimated` modifier, `GetAnimationBaseValue` | none; `Animation_sampler::apply` writes the `Trs_transform` directly, overwriting the authored value | future | Section 6: an animated layer as a second optional in the entry, so playback never clobbers the local value. |
| `HandoffBehavior` (`SnapshotAndReplace`, `Compose`) | none | future | |

### 2.10 Threading

The two systems protect the per-object store in opposite ways.

WPF uses **thread affinity** (ownership). Every `DependencyObject` derives
from `DispatcherObject`, which records the `Dispatcher` (one per thread)
of the thread that constructed it. `GetValue`, `SetValue`,
`SetCurrentValue`, `ClearValue`, `CoerceValue` and `InvalidateProperty`
each begin with `VerifyAccess()`, which throws
`InvalidOperationException` when the calling thread is not that
dispatcher's thread. The store therefore never needs a lock: exactly one
thread can ever touch an object, for the object's whole life. Another
thread reaches it only by posting work to its dispatcher
(`Dispatcher.Invoke` / `BeginInvoke`). The one escape is `Freezable`:
`Freeze()` detaches the object from its dispatcher and makes it
read-only, after which any thread may read it (brushes, geometries and
transforms shared across threads are frozen for this reason). An object
with no dispatcher (constructed on a thread without one) is unchecked.

erhe uses **mutual exclusion** (locking). A `Dependency_object` records
no thread. An item belongs to an `Item_host` (a scene root or a content
library), and the host owns one mutex that guards every item it hosts;
a thread that reads or writes an item's state, its property entries
included, holds `Item_host_lock_guard` on that host for the duration.
Any thread may therefore use any item, workers included (the scene
commit queue prepares items on workers and the main thread flushes
them), as long as it holds the lock; the property library itself
performs no check, so the discipline is the caller's. Expression sources
are resolved through the target's own host (`Item_host::find_hosted_item`),
so an evaluation runs under a single host lock and never needs a second
one. Sealing (2.7) has no threading role: it closes the local layer to
writers, it does not license lock-free reads.

| WPF | erhe | Status | Notes |
|---|---|---|---|
| `DispatcherObject.VerifyAccess()` on every `GetValue` / `SetValue` (thread affinity per object) | none; values are item state under the item host's mutex (`Item_host_lock_guard`) | omitted | Section 5. |
| Registry writes locked (`Synchronized`), reads lock-free; metadata sealed after first use | Registry written during static init + single-threaded startup. The design record describes reads as lock-free; in the current code `Property_registry::find` and `add_owner` take a `std::mutex` | adapted | |
| Frozen `Freezable` usable from any thread | Seal has no threading meaning | adapted | |

### 2.11 Enumeration, type conversion and serialization

| WPF | erhe | Status | Notes |
|---|---|---|---|
| `LocalValueEnumerator` / `LocalValueEntry` | `for_each_local_value`, `Property_set::read_local_values` | ported | |
| Enumerate DPs of a type: reflection / `TypeDescriptor` / `DependencyPropertyDescriptor.FromName` | `Property_registry::for_each_property_of_object(object_type)`, root-first, registration order per level | erhe-only | WPF has no registry query "all DPs applicable to a type"; the registry is queried by name. |
| CLR `enum` + `EnumConverter` | `Enum_value` + `Enum_info` table (label, value) attached to the property | adapted | erhe keeps the enumerator table in the registry so generic code can present and parse it (R9). |
| `TypeConverter` / `ValueSerializer` / `DependencyPropertyValueSerializer` | `to_string` / `parse_value` (`property_string.hpp`) | adapted | One fixed function pair for the 12 types plus enumerations. |
| XAML serialization, `DesignerSerializationOptions` | glTF extras (`ERHE_node` / `ERHE_light` / `ERHE_camera` `properties`, planned `ERHE_*_properties`) | adapted | Local values only; expressions and styles are session state until the extension lands. |
| Value bags: none at the property level | `Property_set` (sorted bag; read, apply, diff, compare) | erhe-only | Serves clipboard, multi-selection and styles (R19). |

### 2.12 Editor and tooling integration

| WPF | erhe | Status | Notes |
|---|---|---|---|
| Undo: none in the property system (apps build their own) | `Property_set_operation` records the exact local state before and after (`Local_state` = value or expression text, or "no local value") | erhe-only | |
| Property inspector: designer-side (Blend / VS) reading `DependencyPropertyDescriptor` | Generic rows in the Properties window from `for_each_property_of_type` + `Property_ui` | erhe-only | |
| Remote / scripted access: none | MCP tools (`get_item_properties`, `set_item_property`, `set_item_style`, ...), `scene.set_property` command | erhe-only | |
| Consequence dispatch in `FrameworkElement.OnPropertyChanged` (`AffectsMeasure` -> `InvalidateMeasure`, ...) | `App_context::on_item_property_changed` reads `Property_flags` | adapted | One hook, in the editor, not in the object. Item-internal consequences use the metadata changed callback (D19). |

## 3. Differences in detail

The rows above condense to these behavioral differences. Each one is a
choice recorded in `doc/property_system.md`; the D-numbers point there.

1. **Fixed value vocabulary.** WPF stores any CLR object; erhe stores a
   12-alternative variant plus enumerations. This removes boxing,
   `DefaultValueFactory`, sub-property changes, `Freezable` value
   semantics, `DeferredReference` and type converters, and makes the
   string form and the UI widget derivable from the type alone (D2, D2a,
   D16).

2. **Owner identity.** Both key by class and walk the class chain. WPF
   takes the class from CLR reflection; erhe allocates a run-local id per
   class from the `Item<>` template, or by hand for classes outside an
   `Item<>` chain and for runtime-defined kinds (D3, D27).

3. **Metadata override replaces, WPF merges.** WPF's `Merge` chains
   `PropertyChangedCallback` delegates (base first) and inherits the base
   default when unset; erhe copies the base default when unset and
   replaces the rest. A derived-type override in erhe that needs the base
   callback must call it itself (D4).

4. **Validate failure is a logged no-op, not an exception.** WPF throws
   `ArgumentException`; erhe logs, returns `false`, and leaves the store
   unchanged so bad file / MCP / script values never abort the editor
   (D7). The same policy covers read-only, sealed and expression errors.

5. **Coerce runs at write time for local values and at read time for
   everything else.** WPF coerces the effective value whenever it is
   re-evaluated from any source and caches the result. erhe stores the
   coerced value only next to a local value; a style, inherited or
   default value is coerced on every read, and `coerce_value` has
   nothing to re-run without a local value. Consequence: a coerced local
   value cannot follow a later parent change, which is why `visible` is
   not WPF's `IsVisible` (D7, D23).

6. **Inherited values are not cached.** WPF copies the inherited value
   into the child's entry and invalidates it through `TreeWalkHelper`.
   erhe walks up on every read and only *notifies* descendants on change
   and reparent (snapshot diff). Cheaper state, costlier deep reads (D8).

7. **Style is one setter bag per object.** No `TargetType`, `BasedOn`,
   triggers, implicit styles, theme styles or resources; five of WPF's
   twelve base value sources collapse into erhe's single `style` (D25).

8. **Expressions are formulas, bindings are one-reference formulas.**
   erhe has no `Binding` object graph: no converters, modes, update
   triggers, validation rules, fallback values, async, or `PropertyPath`
   sub-paths. It adds what WPF lacks: per-component formulas, an explicit
   error string, cycle detection, lazy resolution with pull-on-read, and
   an object-provided name resolver (D22).

9. **Change pipeline order.** WPF: virtual `OnPropertyChanged` (which
   calls the metadata callback) -> expression dependents -> framework
   consequences. erhe: metadata callback -> virtual -> observers ->
   dependents -> descendants. erhe additionally notifies on a source
   change with an unchanged value, and batches through `Change_batch`
   (D9, D15).

10. **Observers are RAII tokens with object-lifetime safety.** WPF's
    `DependencyPropertyDescriptor.AddValueChanged` holds a strong
    reference until removed by hand; erhe's `Observer_token`
    unsubscribes on destruction and is deactivated when the object dies.
    erhe also has an any-property observer (D15, D21).

11. **Sealing is reversible and non-threading.** WPF `Freeze` is
    one-way and buys cross-thread use; erhe `seal` / `unseal` mirrors the
    `lock_edit` flag and only closes the local layer. Inherited
    notifications, observers and installed expressions keep working on a
    sealed object (D24).

12. **Computed instead of stored read-only.** Where WPF owners write a
    read-only DP through its key (`IsVisible`, `ActualWidth`), erhe
    registers a provider called on every read; nothing is stored, no
    changed callback or observer fires, and the owner pushes
    `invalidate_dependents` for expressions (D26).

13. **Bridged storage.** A property can live in the object's own member
    (`Trs_transform`, `Projection`, graph node fields), always reporting
    `local`, never inheriting or taking a style value. WPF has no way to
    keep the DP value outside the entry array (D18).

14. **No thread affinity.** No `Dispatcher`, no `VerifyAccess`; the
    item-host mutex is the rule. The registry's startup write window
    replaces WPF's sealed-after-use metadata (R12).

15. **Consequence flags are data, acted on in one editor hook.** WPF's
    `FrameworkElement` acts on `AffectsMeasure` and friends inside the
    object's own `OnPropertyChanged`; erhe's library never reads the
    flags and the editor hook maps them to rebuilds (D11, R15).

16. **Editor infrastructure is part of the system.** Exact-local-state
    undo, generic rows driven by `Property_ui`, `Property_set` bags for
    clipboard / multi-selection / styles, MCP tools and command scripts
    have no WPF property-system counterpart; WPF leaves them to
    application and designer code (D11, D12, D13, D17).

## 4. WPF features erhe leaves out, by reason

| Omitted (section 5) | Not yet (section 6) |
|---|---|
| Templates, triggers, theme and implicit styles, resources | Animated value layer (`IsAnimated`, `GetAnimationBaseValue`) |
| Two-way / one-way-to-source bindings, `UpdateSourceTrigger`, validation rules, converters | |
| Dispatcher thread affinity | Serialized expressions and styles (`ERHE_*_properties` extension) |
| `UncommonField`, `DeferredReference`, `DefaultValueFactory` | Animation channels on arbitrary properties |
| `AttachedPropertyBrowsable*` attributes, `TypeDescriptor` integration | Metadata callback chaining, if a derived-type override ever needs it |
| `InheritanceBehavior`, `InheritanceContext` for non-tree objects | |
| Sub-property change notification | |

## 5. erhe additions with no WPF counterpart

- `Property_bridge` (member-backed properties).
- Computed properties through a provider (`register_computed`).
- `Property_ui` metadata block and `Visible_when` predicates.
- `Change_batch` with per-property before / after collapsing.
- Any-property observer; RAII observer tokens.
- Formula expressions: per-component, tinyexpr functions, logic set,
  cycle detection, error reporting, lazy resolution with pull-on-read,
  `invalidate_dependents` for out-of-band storage changes.
- `Property_set` value bags and `Local_state` for exact undo.
- `Enum_info` tables in the registry.
- Registry enumeration of the properties of a type in registration order.
- Reversible sealing tied to `lock_edit`.
- Editor and MCP integration (rows, undo operations, tools, commands).
