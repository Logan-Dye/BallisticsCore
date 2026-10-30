# Modular Launch Sources

## Mental Model

A source override hijacks a name.

The launch profile does not reference a barrel, mesh, or attachment directly.
It asks for a named source such as `Muzzle` or `ShellEject`. The launcher and
attached contributors answer which component owns that name right now.

```text
No barrel:     Muzzle -> receiver mesh -> missing socket -> effect skipped
Barrel:        Muzzle -> barrel mesh -> Muzzle socket
Launch device: Muzzle -> device mesh -> Muzzle socket
Suppressor:    Muzzle -> suppressor mesh -> Muzzle socket
```

`BindingName` and `SocketNameOverride` are independent strings. A binding named
`ShellEject` pointing to a socket named `Eject` is normal and correct.

## Receiver

Add `UProjectileLauncherComponent` to the receiver or other actor that owns the
launch. Configure its `LaunchEffectSourceBindings` for sources that exist on
the base actor.

Example:

```text
Binding Name: ShellEject
Source Component: receiver mesh
Effect Socket: Eject
```

## Barrel

Add `UBallisticsLaunchContributorComponent` to the barrel actor. Add an inline
`SourceOverrides` entry:

```text
Binding Name: Muzzle
Component Tag: LaunchSource
Socket Name Override: Muzzle
Override Priority: false
```

Tag the barrel mesh component `LaunchSource`. The contributor finds the nearest
launcher through the attachment ancestry. No cosmetic modifier asset is needed.

## Launch Device and Suppressor

Use the same inline `Muzzle` override on each attachment that physically moves
the launch point. NPS selects the deepest attachment declaring that binding, so
the outermost device wins automatically.

Only enable `Override Priority` for a configuration where physical attachment
depth is deliberately not the desired order. Higher priority wins, then depth
breaks ties.

A suppressor can also assign one `UBallisticsLaunchCosmeticModifier` for its
audio, Niagara, and named parameter changes. Geometry remains inline on the
component, while the modifier describes only the cosmetic change.

Replacing an effect also replaces its typed Niagara parameter arrays. An
attachment can therefore replace a visual role and its mesh, material,
texture, direction, speed, color, or other supported values in one edit.

Cosmetic modifiers follow the same attachment rule as sources. Deeper
contributors apply later, so their conflicting sound overrides, effect edits,
and named parameter overrides win automatically. Equal-depth contributors use
their stable component path as the final tie-breaker.

Enable the modifier's `Override Priority` only when attachment depth is not the
desired cosmetic order. Higher priority applies later, then depth and stable
component path break ties. Existing modifier assets keep their previous
priority ordering when first loaded; disable `Override Priority` on an old
asset to move it to depth ordering.

Modifier-hosted `SourceOverrides` remain supported for existing projects. If
the same contributor declares an inline override and a modifier override for
one binding, the inline entry wins.

## Gameplay Origin

`ResolveLaunchSourceTransform` is a gameplay API, not a cosmetic-only helper.

```cpp
FTransform LaunchTransform;
if (Launcher->ResolveLaunchSourceTransform(
    TEXT("Muzzle"),
    TEXT("Muzzle"),
    LaunchTransform))
{
    Launcher->Launch(
        LaunchTransform.GetLocation(),
        LaunchTransform.GetUnitAxis(EAxis::X));
}
else
{
    // Use the consumer's normal launch-origin fallback.
}
```

If the binding, component tag, or requested socket is missing, the function
returns `false`. It does not substitute the component transform for a missing
socket. An explicit contributor override also fails closed instead of falling
back to a shallower source.

No effect at an attachment? Verify that the component tag resolves to the
intended mesh and that the requested socket exists on that mesh. A missing
socket skips the effect.

## Runtime Attachment Changes

Normal direct attach and detach changes are detected automatically. NPS uses a
constant-time parent check in the steady state and staggers full ancestry
audits across the aggregate update. Call `RefreshLauncherBinding()` after an
unusual reparent when the new binding must be available in the same frame.
