# Architecture.md

**Foundation layer:**
- **llcommon** - Foundation: threading (`LLThread`, `LL::ThreadPool`, `LL::WorkQueue`), logging, string utils, timers, event system (`LLEventPump`), coroutines (`LLCoros`), LLSD (universal data type), `LLSingleton` base class
- **llmath** - Math primitives: vectors, matrices, quaternions, bounding boxes. Also includes mikktspace tangent generation and meshoptimizer

**Data/IO layer:**
- **llfilesystem** - Virtual filesystem and asset cache
- **llxml** - XML parsing utilities (depends on llfilesystem)
- **llimage** - Image decoding/encoding (JPEG, PNG, TGA, J2C via OpenJPEG or KDU)
- **llimagej2coj** / **llkdu** - JPEG2000 codec implementations (OpenJPEG open-source, KDU proprietary optional)

**Network layer:**
- **llcorehttp** - Async HTTP client library (libcurl + OpenSSL + websocketpp). Used pervasively for API calls and asset fetches
- **llmessage** - Network protocol layer for communicating with Second Life servers. Includes UDP message system (`LLMessageSystem`) with template-based message definitions, and `LLAssetStorage` base

**Rendering layer:**
- **llrender** - OpenGL abstraction: shaders (`LLGLSLShader`, `LLShaderMgr`), vertex buffers (`LLVertexBuffer`), render targets (`LLRenderTarget`), textures, font rendering (FreeType + HarfBuzz)
- **llwindow** - Platform window management and input (native Win32/Cocoa/X11 and SDL3 on Linux). Depends on llrender

**Content model layer:**
- **llinventory** - Inventory item/folder data model
- **llcharacter** - Avatar skeleton, joints, animation
- **llprimitive** - 3D primitive object data model (prims, sculpts, mesh, GLTF via tinygltf, collision via VHACD)
- **llappearance** - Avatar appearance/baking (depends on llcharacter, llinventory, llimage, llrender)
- **llaudio** - Audio engine abstraction (OpenAL or FMOD Studio, Vorbis decoding)

**UI layer:**
- **llui** - UI widget framework: panels, floaters, buttons, text editors, lists, toolbars, notifications. Layout defined in XUI (XML) files. Includes spell-check via Hunspell

**Application layer:**
- **lllogin** - Login subsystem
- **llplugin** - Plugin framework for out-of-process media (CEF browser, VLC)
- **llwebrtc** - WebRTC voice integration (conditional)
- **llphysicsextensionsos** - Physics extensions stub (VHACD convex decomposition)
- **media_plugins** - Out-of-process media plugin implementations (CEF, VLC, GStreamer)
- **newview** - The main viewer application. Contains all viewer-specific logic: the rendering pipeline, scene graph, UI floaters/panels, settings, object/avatar systems, and the application entry point. Links against all libraries above plus third-party: Tracy, OpenXR, Discord SDK, NVAPI, TinyEXR, Velopack

### Application Lifecycle

The viewer application is `LLAppViewer` (derives from `LLApp`) in `indra/newview/llappviewer.h/.cpp`. The lifecycle is three methods:

1. **`init()`** — Cache, window creation, graphics/feature detection, worker thread creation (texture cache, image decode, texture fetch), singleton initialization, event system setup
2. **`frame()`** — Called in a loop. Processes events/messages, updates objects/regions/avatars, renders the frame, sleeps to maintain FPS cap
3. **`cleanup()`** — Shuts down worker threads, disconnects from simulators, saves settings/cache, releases resources

### Singleton Pattern

The codebase uses `LLSingleton<T>` (`indra/llcommon/llsingleton.h`) extensively. It automatically tracks dependencies between singletons (by monitoring `getInstance()` calls during construction) and shuts them down in reverse dependency order. States: `UNINITIALIZED → CONSTRUCTING → INITIALIZING → INITIALIZED → DELETED`. A lighter `LLSimpleton<T>` variant is used by many Alchemy-specific classes.

### Event System & LLSD

**LLSD** (`indra/llcommon/llsd.h`) is the universal data container used for event messages, network messages, settings, HTTP bodies, and capability responses. Supports: Boolean, Integer, Real, UUID, String, Date, URI, Binary, Map, Array, and Undefined. Value semantics. Not thread-safe — transfer ownership across threads.

**LLEventPump** (`indra/llcommon/llevents.h`) is the pub/sub event bus. Subsystems communicate by posting LLSD events to named pumps. Listeners return true to stop propagation. `LLEventAPI` wraps C++ APIs as event-driven interfaces with introspection. The `LLEventDispatcher` dispatches events to methods by string name.

### Threading & Coroutines

**Threads:** `LLThread` (basic), `LLQueuedThread`/`LLWorkerThread` (request-queue based), `LL::ThreadPool` + `LL::WorkQueue` (modern work dispatch with configurable pool sizes via `ThreadPoolSizes` setting). Work queues support `post()` (fire-and-forget), `postTo()` (handshake between queues), and `waitForResult()`.

**Coroutines:** `LLCoros` singleton (`indra/llcommon/llcoros.h`) manages `boost::fibers::fiber` instances. Launched via `LLCoros::instance().launch("name", callable)` — always pass parameters by value. Key primitives in `lleventcoro.h`: `llcoro::suspend()` (yield one tick), `suspendUntilTimeout(seconds)`, `postAndSuspend(event, request_pump, reply_pump)`. Coroutines are the preferred pattern for async work, replacing older callback-based approaches.

**Mutexes** are boost::fiber-aware (`boost::fibers::mutex`, `boost::fibers::condition_variable`) so coroutines and threads interoperate correctly.

### Network & Region System

**UDP Messages:** `LLMessageSystem` (`indra/llmessage/message.h`) handles viewer↔simulator communication using template-defined message formats with zerocoding, reliable/unreliable delivery, acks, and resends. `LLCircuitData` (`llcircuit.h`) tracks per-simulator connection state (packet IDs, ping, throttle buckets).

**Regions:** `LLViewerRegion` (`indra/newview/llviewerregion.h`) represents a 256×256m simulator region containing terrain, objects, spatial partitions, capabilities (HTTP API endpoints), and an object cache (~50k objects). `LLWorld` singleton manages the set of nearby regions as the agent moves.

**Assets:** `LLViewerAssetStorage` (`indra/newview/llviewerassetstorage.h`) fetches assets via coroutine-based HTTP requests using capability URLs from the region. Textures have a dedicated pipeline: `LLTextureFetch` (worker thread) → `LLImageDecodeThread` → `LLTextureCache` (disk) → `LLViewerTextureList` (in-memory).

### Viewer Object System

`LLViewerObject` (`indra/newview/llviewerobject.h`) is the base for all world objects, deriving from `LLPrimitive`. Key subclasses:
- **`LLVOVolume`** (`llvovolume.h`) — Most common type: prims, meshes, sculpts. Contains volume geometry with multiple faces, materials, texture animation, media-on-surface, flex
- **`LLVOAvatar`** (`llvoavatar.h`) — Avatar with skeletal system, mesh deformation, animation blending, appearance layering, impostor rendering. `LLVOAvatarSelf` is the player's own avatar
- **`LLVOGrass`**, **`LLVOTree`**, **`LLVOWater`**, **`LLVOSurfacePatch`** (terrain), **`LLVOPartGroup`** (particles), **`LLVOWLSky`** (sky dome)

`LLViewerObjectList` (`llviewerobjectlist.h`) is the global registry tracking all objects by UUID.

### Rendering Pipeline

The deferred rendering pipeline is orchestrated by `LLPipeline` (`indra/newview/pipeline.h/.cpp`).

**Render passes** execute in this order:
1. **Geometry/GBuffer** (`renderGeomDeferred()`) — Writes to multiple render targets
2. **Shadows** (`renderGeomShadow()`) — 4 sun shadow cascades + 2 spot light shadow maps
3. **Deferred Lighting** (`renderDeferredLighting()`) — Sun, point lights (up to 16 batched), spot lights, with reflection probe influence
4. **Post-Deferred** (`renderGeomPostDeferred()`) — Alpha, water, atmospheric haze
5. **Post-Processing** — Luminance/exposure, bloom, DoF, tonemapping, color grading, AA, screen effects
6. **Finalize** (`renderFinalize()`) — Final blit with vignette, film grain, dithering, chromatic aberration

**GBuffer layout** (MRT attachments on `deferredScreen`):
- frag_data[0]: Base color (GL_RGBA)
- frag_data[1]: Specular/ORM — PBR occlusion/roughness/metallic (GL_RGBA)
- frag_data[2]: Normals (GL_RGBA16 or GL_RGB10_A2)
- frag_data[3]: Emissive (GL_R11F_G11F_B10F, optional via `RenderEnableEmissiveBuffer`)

**Render target packs** (`RenderTargetPack` in pipeline.h):
- `mMainRT` — Full resolution for main scene
- `mAuxillaryRT` — 512×512 for reflection probes and dynamic texture bakes
- `mHeroProbeRT` — High-res hero probe rendering
- Additional targets: `mSceneMap` (SSR input), `mLuminanceMap`/`mExposureMap` (auto-exposure), `mPostPingMap`/`mPostPongMap` (post-process ping-pong), `mFXAAMap`, `mSMAABlendBuffer`, `mGlow[3]` (bloom pyramid), `mWaterDis` (refraction), `mSpotShadow[2]`, `mPbrBrdfLut`

**Post-processing chain:**
- **Auto-exposure:** Progressive histogram (`gLuminanceProgram`, `gExposureProgram`) with history fade
- **Bloom:** Bright-area extraction → 3-level glow pyramid (`mGlow[3]`) with warmth correction
- **Depth of Field:** Circle-of-confusion via `gDeferredCoFProgram`, combine via `gDeferredDoFCombineProgram`. Settings: `CameraFNumber`, `CameraFocalLength`, `CameraMaxCoF`
- **Screen Space Reflections:** Class 3+ feature, iterative ray marching
- **Tonemapping:** ACES, Reinhard, Filmic, AGX — selectable via `AlchemyRenderTonemapType`
- **Color Grading:** 3D LUT-based (`gDeferredPostGammaCorrectCGLutProgram`, `mCGLut`)
- **Anti-aliasing:** FXAA (1-pass, `gFXAAProgram[4]`) or SMAA (3-pass edge detect → blend weights → neighborhood blend, `gSMAAEdgeDetectProgram[4]`/`gSMAABlendWeightsProgram[4]`/`gSMAANeighborhoodBlendProgram[4]`). CAS (Contrast Adaptive Sharpening) via `gCASProgram`
- **Final blit effects** (`blitWithEffectsF.glsl` in `shaders/class1/alchemy/`): Vignette (configurable shape/softness/color), film grain (luma/color/coarse/photon styles), TPDF dithering, CVD compensation/preview
- **Chromatic aberration** (`colorCorrectF.glsl` in `shaders/class1/alchemy/`): Per-channel offset with amount, falloff, angle, anisotropy controls

### Shader System

Shader management has two layers:
- `LLShaderMgr` (`indra/llrender/llshadermgr.h/.cpp`) — Base shader manager with uniform enums, binding, and feature defines
- `LLViewerShaderMgr` (`indra/newview/llviewershadermgr.h/.cpp`) — Viewer-specific shader loading, program definitions, and quality class selection

GLSL shaders live in `indra/newview/app_settings/shaders/` organized by quality class and category:
- **`class1/`** — Base shaders (all hardware)
- **`class2/`** — Mid-tier features
- **`class3/`** — Advanced features (SSR, high-quality lighting)
- Categories: `deferred/`, `objects/`, `environment/`, `alchemy/` (Alchemy post-processing), `windlight/`, `avatar/`, `interface/`

Key shader groups: GBuffer write (`gDeferredDiffuseProgram`, `gDeferredPBROpaqueProgram`, `gDeferredBumpProgram`, `gDeferredMaterialProgram[]`, etc.), deferred lighting (`gDeferredSunProgram`, `gDeferredLightProgram`, `gDeferredMultiLightProgram[16]`, `gDeferredSpotLightProgram`), shadows (`gDeferredShadowProgram` and variants), environment (`gDeferredWLSkyProgram`, `gWaterProgram`, `gHazeProgram`), post-processing (tonemap, FXAA, SMAA, CAS, bloom, DoF programs).

### Draw Pool & Spatial System

**Draw pools** (`indra/newview/lldrawpool.h` and `lldrawpool*.h`) group geometry by material type for batched rendering. Each pool corresponds to a render pass. Key pools: `POOL_SIMPLE`, `POOL_BUMP`, `POOL_MATERIALS` (legacy), `POOL_GLTF_PBR` (PBR opaque), `POOL_GLTF_PBR_ALPHA_MASK`, `POOL_TERRAIN`, `POOL_AVATAR`, `POOL_CONTROL_AV` (animesh), `POOL_ALPHA_PRE_WATER`/`POOL_ALPHA_POST_WATER`, `POOL_WATER`, `POOL_WL_SKY`, `POOL_GLOW`. Many pools have `_RIGGED` pass variants for skinned geometry.

**Spatial partitioning** uses an octree. `LLSpatialPartition` (`indra/newview/llspatialpartition.h`) is the base, with specialized partitions: `LLVolumePartition`, `LLTerrainPartition`, `LLTreePartition`, `LLWaterPartition`, `LLGrassPartition`, `LLParticlePartition`. `LLSpatialGroup` is an octree node grouping drawable geometry. `LLDrawInfo` encapsulates a single draw call (vertex buffer, index range, textures, material, blend mode, skin info). `LLCullResult` stores per-frame visible geometry organized by render type.

### PBR Materials & Reflection Probes

**PBR:** GLTF PBR metallic-roughness workflow. `LLFetchedGLTFMaterial` (`llfetchedgltfmaterial.h`) wraps material data (base color, normal, metallic/roughness, emissive, occlusion textures). `LLGLTFMaterialList` manages the material registry. Terrain also supports PBR via `gDeferredPBRTerrainProgram[]`. BRDF LUT generated at startup via `gDeferredGenBrdfLutProgram`.

**Reflection probes:** `LLReflectionMapManager` (`llreflectionmapmanager.h`) manages up to 256 probes with box/sphere shapes, stored in cubemap arrays. Resolution: 128×128 radiance, 16×16 irradiance. Probes are blended via neighbor tracking. `LLHeroProbeManager` (`llheroprobemanager.h`) handles up to 2 high-quality planar probes at 1024×1024 for mirrors and reflective surfaces. Probe data is bound as a UBO during the lighting pass.

### Environment System

`LLEnvironment` singleton (`indra/newview/llenvironment.h`) manages sky, water, and day cycle settings. `LLSettingsSky` controls sun/moon direction, color, haze, clouds. `LLSettingsWater` controls fog, waves, reflectivity. `LLSettingsDay` defines 4-track day cycles with altitude-based track selection and time-based interpolation.

Environment selection priority (highest to lowest): edit environment → local → parcel → region → default.

Sky rendering: `gDeferredWLSkyProgram` (sky dome), `gDeferredWLCloudProgram` (clouds), `gDeferredWLSunProgram`/`gDeferredWLMoonProgram` (celestial bodies), `gDeferredStarProgram` (stars). Water: `gWaterProgram` (surface with reflections/refractions), `gUnderWaterProgram` (underwater fog), distortion texture for wave animation.

### UI Framework

The widget hierarchy in `indra/llui/`: `LLView` (root, handles mouse events, drawing, view tree, `FOLLOWS_*` anchor flags) → `LLUICtrl` (interactive controls) → `LLPanel` (general container with borders/backgrounds) → `LLFloater` (dockable window with title bar, close/minimize/maximize). Other widgets: `LLButton`, `LLLineEditor`, `LLTextEditor`, `LLScrollListCtrl`, `LLComboBox`, `LLSliderCtrl`, `LLTabContainer`, `LLLayoutStack`, `LLToolBar`, `LLAccordionCtrl`, `LLFlatListView`.

**XUI system:** `LLUICtrlFactory` singleton loads XML layout files and instantiates widget trees. `LLXUIParser` parses XUI. Widget types are registered via `LLChildRegistry<T>::Register<T>` for static type registration. The `LLInitParam` system provides declarative parameter blocks for widget configuration.

**Floater management:** `LLFloaterReg` singleton maps floater names → build functions and instance lists. Template method `LLFloaterReg::build<T>(key)` instantiates floaters. `LLDockableFloater` for docking, `LLMultiFloater` for tabbed containers.

**Notifications:** `LLNotifications` singleton (`indra/llui/llnotifications.h`) manages a prioritized queue with channel-based hierarchy. Templates loaded from XML. `LLNotificationsUtil::add()` is the public API.

### Settings System

Viewer settings are XML-defined in `indra/newview/app_settings/`:
- `settings.xml` — Main viewer settings (inherited from upstream)
- `settings_alchemy.xml` — Alchemy-specific settings
- `settings_per_account.xml` / `settings_per_account_alchemy.xml` — Per-user settings

### UI Skins

UI is defined via XUI (XML User Interface) files in `indra/newview/skins/`. Multiple skins exist: `default`, `alchemy`, `gemini`, `heretic`, `ionic`. Each skin contains `xui/` (XML layouts), `textures/`, and theme files. English strings are in `xui/en/`. Skins support language-specific XUI overrides (en, de, es, fr, ja, it, pt, ru, tr, zh).

### Tracy Profiling

Tracy profiler support is built into the project. It is enabled by default for "Test" channel builds (controlled by `USE_TRACY`). Use `USE_TRACY_ON_DEMAND=ON` (default) to only profile when a Tracy server connects.
