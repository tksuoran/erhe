# Chapter 5 Shared Objects and Multiple Contexts

This chapter describes special considerations for objects shared between multiple
OpenGL contexts, including deletion behavior and how changes to shared objects
are propagated between contexts.

Objects that may be shared between contexts include buffer objects, program
and shader objects, renderbuffer objects, sampler objects, sync objects, and texture
objects (except for the texture objects named zero).

Some of these objects may contain views (alternate interpretations) of part or
all of the data store of another object. Examples are texture buffer objects, which
contain a view of a buffer object’s data store, and texture views, which contain a
view of another texture object’s data store. Views act as references on the object
whose data store is viewed.

Objects which contain references to other objects include framebuffer, program
pipeline, transform feedback, and vertex array objects. Such objects are called
container objects and are not shared.

Implementations may allow sharing between contexts implementing different OpenGL
versions or different profiles of the same OpenGL version (see appendix E).
However, implementation-dependent behavior may result when aspects and/or behaviors
of such shared objects do not apply to, and/or are not described by more than one
version or profile.

## 5.1 Object Deletion Behavior

### 5.1.1 Side Effects of Shared Context Destruction

The share list is the group of all contexts which share objects. If a shared object
is not explicitly deleted, then destruction of any individual context has no effect
on that object unless it is the only remaining context in the share list. Once the
last context on the share list is destroyed, all shared objects, and all other resources
allocated for that context or share list, will be deleted and reclaimed by the imple-
mentation as soon as possible.

### 5.1.2 Automatic Unbinding of Deleted Objects

When a buffer, texture, or renderbuffer object is deleted, it is unbound from any
bind points it is bound to in the current context, and detached from any attachments
of container objects that are bound to the current context, as described for 
`DeleteBuffers`, `DeleteTextures`, and `DeleteRenderbuffers`. If the object binding was
established with other related state (such as a buffer range in `BindBufferRange` or
selected level and layer information in `FramebufferTexture` or `BindImageTexture`),
all such related state are restored to default values by the automatic unbind.
Bind points in other contexts are not affected. Attachments to unbound container
objects, such as deletion of a buffer attached to a vertex array object which is not
bound to the context, are not affected and continue to act as references on the
deleted object, as described in the following section.

### 5.1.3 Deleted Object and Object Name Lifetimes

When a buffer, texture, sampler, renderbuffer, query, or sync object is deleted,
its name immediately becomes invalid (e.g. is marked unused), but the underlying
object will not be deleted until it is no longer in use.

A buffer, texture, sampler, or renderbuffer object is in use if any of the following
conditions are satisfied:

- the object is attached to any container object
- the object is bound to a context bind point in any context
- any other object contains a view of the data store of the object.

A sync object is in use while there is a corresponding fence command which
has not yet completed and signaled the sync object, or while there are any GL
clients and/or servers blocked on the sync object as a result of `ClientWaitSync`
or `WaitSync` commands.

Query objects are in use so long as they are active, as described in section 4.2.

When a shader object or program object is deleted, it is flagged for deletion, but
its name remains valid until the underlying object can be deleted because it is no
longer in use. A shader object is in use while it is attached to any program object.
A program object is in use while it is attached to any program pipeline object or is
a current program in any context.

Caution should be taken when deleting an object while it is in use (as defined
above). Following its deletion, the object’s name may be returned by `Gen*` or
`Create*` commands. The underlying object state and data for such a deleted, but
still in use object may still be read and written by the GL, but cannot be accessed
by name. The underlying storage backing a deleted object will not be reclaimed by
the GL until all references to the object from container object attachment points,
context binding points, views, fence commands, active queries, etc. are removed.
Since the name is marked unused, binding the name will create a new object with
the same name, and attaching the name will generate an error.

5.2 Sync Objects and Multiple Contexts

When multiple GL clients and/or servers are blocked on a single sync object and
that sync object is signaled, all such blocks are released. The order in which
blocks are released is implementation-dependent.

5.3 Propagating Changes to Objects

GL objects contain two types of information, data and state. Collectively these
are referred to below as the contents of an object. For the purposes of propagating
changes to object contents as described below, data and state are treated consis-
tently.

Data is information the GL implementation does not have to inspect, and does
not have an operational effect. Currently, data consists of:

- Pixels in the framebuffer.
- The contents of the data stores of buffer objects, renderbuffers, and textures.

State determines the configuration of the rendering pipeline, and the GL imple-
mentation does have to inspect it.

In hardware-accelerated GL implementations, state typically lives in GPU registers,
while data typically lives in GPU memory.

When the contents of an object T are changed, such changes are not always
immediately visible, and do not always immediately affect GL operations involving
that object. Changes may occur via any of the following means:

- State-setting commands, such as `TexParameter`.

- Data-setting commands, such as `TexSubImage*` or `BufferSubData`.

- Data-setting through rendering to renderbuffers or textures attached to a
  framebuffer object.

- Data-setting through transform feedback operations followed by an `EndTransformFeedback`
  command.

- Commands that affect both state and data, such as `TexImage*` and `BufferData`.

- Changes to mapped buffer data followed by a command such as `UnmapBuffer` or
  `FlushMappedBufferRange`.

- Rendering commands that trigger shader invocations, where the shader performs image or
  buffer variable stores or atomic operations, or built-in atomic counter functions.

When T is a texture, the contents of T are construed to include the contents of
the data store of T, even if T’s data store was modified via a different view of
the data store.

5.3.1 Determining Completion of Changes to an object

The contents of an object T are considered to have been changed once a command
such as described in section 5.3 has completed. Completion of a command [See note 1]
may be determined either by calling `Finish`, or by calling `FenceSync` and executing
a `WaitSync` command on the associated sync object. The second method does not
require a round trip to the GL server and may be more efficient, particularly when
changes to T in one context must be known to have completed before executing
commands dependent on those changes in another context. In cases where a feedback
loop has been established (see sections 8.6.1, 8.14.2.1, and 9.3, as well as the
discussion of rule 1 below in section 5.3.3) the resulting contents of an object may
be undefined.

[Note 1] The GL already specifies that a single context processes commands in the order
they are received. This means that a change to an object in a context at time t must be
completed by the time a command issued in the same context at time t + 1 uses the result
of that change.

5.3.2 Definitions

In the remainder of this section, the following terminology is used:

- An object T is directly attached to the current context if it has been bound to
  one of the context binding points. Examples include but are not limited to bound
  textures, bound framebuffers, bound vertex arrays, and current programs.

- T is indirectly attached to the current context if it is attached to another
  object C, referred to as a container object, and C is itself directly or indirectly
  attached. Examples include but are not limited to renderbuffers or textures attached
  to framebuffers; buffers attached to vertex arrays; and shaders attached to programs.

- An object T which is directly attached to the current context may be re-attached by
  re-binding T at the same bind point. An object T which is indirectly attached to the
  current context may be re-attached by re-attaching the container object C to which
  T is attached.

  Corollary: re-binding C to the current context re-attaches C and its hierarchy
  of contained objects.

5.3.3 Rules

The following rules must be obeyed by all GL implementations:

- Rule 1: If the contents of an object T are changed in the current context while T is
  directly or indirectly attached, then all operations on T will use the new contents
  in the current context.

  Note: The intent of this rule is to address changes in a single context only. The
  multi-context case is handled by the other rules.

  Note: “Updates” via rendering or transform feedback are treated consistently
  with updates via GL commands. Once `EndTransformFeedback` has been issued,
  any subsequent command in the same context that uses the results of the transform
  feedback operation will see the results. If a feedback loop is setup between
  rendering and transform feedback (see section 13.3.3), results will be undefined.

- Rule 2: While a container object C is bound, any changes made to the contents of
  C’s attachments in the current context are guaranteed to be seen. To guarantee seeing
  changes made in another context to objects attached to C, such changes must be
  completed in that other context (see section 5.3.1) prior to C being bound. Changes
  made in another context but not determined to have completed as described in section
  5.3.1, or after C is bound in the current context, are not guaranteed to be seen.

- Rule 3: Changes to the contents of shared objects are not automatically propagated
  between contexts. If the contents of a shared object T are changed in a
  context other than the current context, and T is already directly or indirectly
  attached to the current context, any operations on the current context involving T via
  those attachments are not guaranteed to use its new contents.

- Rule 4: If the contents of an object T are changed in a context other than the
  current context, T must be attached or re-attached to at least one binding point in
  the current context, or at least one attachment point of a currently bound container
  object C, in order to guarantee that the new contents of T are visible in the current
  context.

  Note: “Attached or re-attached” means either attaching an object to a binding
  point it wasn’t already attached to, or attaching an object again to a binding point
  it was already attached.

  Example: If a texture image is bound to multiple texture bind points and the
  texture is changed in another context, re-binding the texture at any one of the texture
  bind points is sufficient to cause the changes to be visible at all texture bind
  points.
