# AI Camera Director

## Architecture

The feature is an optional UI layer over the existing `QCamera` preview and
`CameraControlBackend`. It does not open a second camera or replace the platform
backend.

1. The user enables the sharing checkbox and submits a prompt.
2. The UI builds a context from the selected device name, camera control descriptors
   and current values, current software-effect settings, and the most recent valid
   local image-quality measurements.
3. `GroqClient` posts a bounded chat-completions request to Groq over HTTPS using
   `QNetworkAccessManager`. The request uses tool calling and is asynchronous. At
   most one local read-tool round trip is made; there are no autonomous retries.
4. `CameraDirectorTools` rejects unknown tools, extra parameters, invalid types,
   unsupported controls, out-of-range values, and values that do not match the
   advertised control step or menu choices.
5. Valid write calls are presented as proposals. They are not executed until the
   user presses **Apply Settings**, or enables **Auto-Apply validated proposals**.
6. Camera writes go through the existing native backend. Its returned accepted value
   is used to update the visible control row. A failed operation is surfaced; the
   app does not tell the user that an unaccepted value was applied.
7. An in-memory settings/effects snapshot is kept before an approved change for
   **Undo**. **Reset to Original** uses the initial values observed after selecting
   the camera.
8. If a valid baseline exists, the assistant panel reports the next fresh sampled
   camera-frame metrics beside the baseline. This is a numeric observation, not an
   overall score or an assertion that the result is objectively better.

The initial model selection is `openai/gpt-oss-20b`. In **Settings → AI Provider →
Groq**, users can refresh the account's model list and select a model. A selection
is not treated as tool-capable based on its name: the application authenticates
against `/openai/v1/models`, then sends a minimal forced-tool request to verify the
selected model's tool-calling workflow before marking the account connected.

## Data handling and credentials

- The user must explicitly enable the sharing checkbox before the app sends a
  request. It is unchecked by default.
- Request context contains text, capability metadata, setting values, and numeric
  image-quality results only. **No camera pixels, image files, or photos are sent.**
- The key is entered in **Settings → AI Provider → Groq** and stored only in
  Windows Credential Manager. Native desktop builds currently target Windows.
- The production credential is stored with service `CamDoctor` and account
  `groq-api-key`.
- Credentials are read from the OS vault only when testing or sending a request.
  The key is never written to QSettings, environment variables, `.env`, logs, or
  diagnostic reports. There is no plaintext fallback. The password field is blank
  when the settings dialog opens; a saved key is never displayed back to the user.
- **Test Connection** validates authentication and the selected model's actual
  tool-call response. **Save & Connect** performs the same test before writing the
  credential to the OS vault. Storage failures are surfaced, and removing the key
  disconnects the director.
- The selected model and Auto-Apply preference are non-secret QSettings values.
  Auto-Apply starts off unless the user previously enabled it. AI sharing consent
  is separate and remains unchecked by default.
- The default endpoint is `https://api.groq.com/openai/v1/chat/completions`.
  Production requests require HTTPS and normal TLS verification.
- Requests are limited to 64 KiB, tool calls to eight per response, prompts to
  1,500 characters, responses to 1 MiB, and network operations to 30 seconds.
  Requests can be cancelled.
  HTTP 401/403, 429, network failures, timeouts, malformed JSON, and invalid tool
  arguments are surfaced explicitly. No automatic retry occurs.
- The Groq client tests use a local mock HTTP server and a test-only key; they do not
  call Groq or use a real credential.
- Windows credential-store tests use a random test-only service name and remove
  their entry after exercising save, read, replacement, and delete. They do not
  touch the production `CamDoctor/groq-api-key` entry.

## Tool allowlist

| Tool | Behavior |
|---|---|
| `get_camera_capabilities()` | Reads the native backend's currently exposed control IDs, ranges, steps, modes, options, and writable status. |
| `get_current_settings()` | Reads current values and automatic/manual flags. |
| `get_image_metrics()` | Reads the latest valid local metrics; unavailable values stay unavailable. |
| `set_exposure(value)` | Proposes an integer value within the selected camera's advertised range and step. Auto exposure is disabled only through the supported backend value. |
| `set_focus_mode(mode)` | Proposes `auto` or `manual` only where a focus-auto control or an auto-capable focus control is exposed. |
| `set_white_balance(value)` | Proposes `auto`, `manual`, or a camera-supported integer temperature. |
| `apply_image_effects(...)` | Proposes bounded local effect values for grayscale, brightness, contrast, saturation, warmth/cool tint, and mild sharpening. |
| `restore_settings(snapshot_id)` | Proposes restoring only the latest in-memory snapshot (`snapshot_id: "latest"`). It cannot name arbitrary files or settings. |

Tool requests never grant code execution, shell access, file access, or direct
network access. Unknown names and extra arguments are rejected. Tool output is
untrusted input; local validation is mandatory even when the model claims a value
is supported.

`capture_photo()` is deliberately not in the model's tool list. The separate
**Save preview photo…** button is a direct user action; the user selects the output
path. It processes the latest local preview frame using the same effect configuration
as the preview.

## Effects, preview, and limitations

`ImageEffectsProcessor` operates on real `QImage` pixels. Its deterministic ranges
are brightness `[-100, 100]`, contrast `[0.5, 1.5]`, saturation `[0, 2]`, and
warmth `[-40, 40]`. Optional sharpening is a mild four-neighbor Laplacian-based
edge enhancement. Original preview pixels are not modified; the application keeps
only the latest frame needed for the preview and explicit save action.

Effect preview work runs in the Qt thread pool. At most one render is active and
there is at most one replaceable pending frame, so old work is not accumulated.
Image-quality diagnostics continue to analyze the unmodified camera frame; that
keeps existing measurement semantics stable when display effects are enabled.
Consequently, a post-change camera-metric comparison does not measure the effect
pipeline itself and is labelled accordingly.

Digital effects are not camera hardware controls. Digital sharpening cannot recreate
information lost to defocus, motion blur, low resolution, or sensor noise. The AI
may describe likely trade-offs, but the application does not claim that a generated
style is objectively better. Hardware support and write/read-back semantics remain
device- and driver-specific.

## Verification

The Windows Debug and Release builds passed, and all 11 CTest targets passed in
each configuration. The automated set includes camera-performance, image-quality,
effect and tool-validation tests; mocked Groq authentication/model/tool-probe
success, invalid credentials, permission denial, rate limiting, malformed data,
timeout, cancellation, and sequential requests; and isolated Windows Credential
Manager save/read/replace/remove/reopen tests.

The backend smoke-test target passed. No live Groq request was made; live behavior
requires a user-provided key. Tests do not verify every physical camera driver's
handling of focus, exposure, or white balance, nor do they replace manual visual
checks on each supported device. The new settings dialog was compile- and
test-validated but not manually exercised against Groq in this environment.
