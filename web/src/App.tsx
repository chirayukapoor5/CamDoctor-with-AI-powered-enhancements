import {
  useCallback,
  useEffect,
  useRef,
  useState,
  type ChangeEvent,
} from "react";
import {
  Badge,
  Button,
  Card,
  CardHeader,
  Checkbox,
  Field,
  Input,
  Select,
  Slider,
  Text,
  Textarea,
  Title1,
  Title2,
} from "@fluentui/react-components";
import {
  ArrowDownloadRegular,
  ArrowResetRegular,
  ArrowUndoRegular,
  CameraRegular,
  LockClosedRegular,
  SparkleRegular,
  VideoRegular,
} from "@fluentui/react-icons";
import { processImageData } from "./imageEffects";
import {
  DEFAULT_EFFECTS,
  GROQ_MODELS,
  type ChatResult,
  type ImageEffects,
  type ProposedAdjustments,
} from "./types";

type CameraState = "idle" | "active" | "denied" | "unavailable" | "unsupported";
type ChatMessage = { role: "user" | "assistant"; text: string };

const repoUrl = "https://github.com/chirayukapoor5/CamDoctor-with-AI-powered-enhancements";
const nativeBuildUrl = `${repoUrl}#build-the-native-application`;

function cameraError(error: unknown): { state: CameraState; message: string } {
  if (error instanceof DOMException && error.name === "NotAllowedError") {
    return { state: "denied", message: "Camera permission was denied. Allow camera access in your browser settings to try again." };
  }
  if (error instanceof DOMException && error.name === "NotFoundError") {
    return { state: "unavailable", message: "No connected camera was found. You can still explore the browser experience with a camera connected." };
  }
  if (error instanceof DOMException && error.name === "NotReadableError") {
    return { state: "unavailable", message: "The camera is busy or unavailable. Close other apps using it and try again." };
  }
  return { state: "unavailable", message: "The camera could not be started. Check browser permissions and try again." };
}

function App() {
  const videoRef = useRef<HTMLVideoElement>(null);
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const capturedFrameRef = useRef<HTMLCanvasElement | null>(null);
  const streamRef = useRef<MediaStream | null>(null);
  const frameRequestRef = useRef<number>(0);
  const historyRef = useRef<ImageEffects[]>([]);
  const editingRef = useRef(false);

  const [cameraState, setCameraState] = useState<CameraState>("idle");
  const [cameraMessage, setCameraMessage] = useState("Your camera stays in this browser.");
  const [effects, setEffects] = useState<ImageEffects>(DEFAULT_EFFECTS);
  const [captured, setCaptured] = useState(false);
  const [hasFrame, setHasFrame] = useState(false);
  const [apiKey, setApiKey] = useState("");
  const [showKey, setShowKey] = useState(false);
  const [model, setModel] = useState<string>(GROQ_MODELS[0].id);
  const [connectionStatus, setConnectionStatus] = useState("Key stays in memory only");
  const [connectionBusy, setConnectionBusy] = useState(false);
  const [prompt, setPrompt] = useState("");
  const [chatBusy, setChatBusy] = useState(false);
  const [chatMessages, setChatMessages] = useState<ChatMessage[]>([]);
  const [proposal, setProposal] = useState<ProposedAdjustments | null>(null);
  const [requestError, setRequestError] = useState("");

  const beginEdit = useCallback(() => {
    if (!editingRef.current) {
      historyRef.current.push({ ...effects });
      if (historyRef.current.length > 30) historyRef.current.shift();
      editingRef.current = true;
    }
  }, [effects]);

  const endEdit = useCallback(() => {
    editingRef.current = false;
  }, []);

  const updateEffect = useCallback((key: keyof ImageEffects, value: boolean | number) => {
    if (!editingRef.current) {
      historyRef.current.push({ ...effects });
      if (historyRef.current.length > 30) historyRef.current.shift();
    }
    setEffects((current) => ({ ...current, [key]: value }));
  }, [effects]);

  const restoreEffects = useCallback((next: ImageEffects) => {
    historyRef.current.push({ ...effects });
    if (historyRef.current.length > 30) historyRef.current.shift();
    setEffects(next);
  }, [effects]);

  const startCamera = useCallback(async () => {
    if (!navigator.mediaDevices?.getUserMedia) {
      setCameraState("unsupported");
      setCameraMessage("This browser does not support webcam access. Try a current version of Chrome, Edge, or Firefox.");
      return;
    }
    setCameraMessage("Waiting for camera permission…");
    let stream: MediaStream | null = null;
    try {
      stream = await navigator.mediaDevices.getUserMedia({
        audio: false,
        video: { facingMode: "user", width: { ideal: 1280 }, height: { ideal: 720 } },
      });
      streamRef.current?.getTracks().forEach((track) => track.stop());
      streamRef.current = stream;
      if (videoRef.current) {
        videoRef.current.srcObject = stream;
        await videoRef.current.play();
      }
      capturedFrameRef.current = null;
      setCaptured(false);
      setHasFrame(false);
      setCameraState("active");
      setCameraMessage("Camera is on. Preview and effects are processed locally.");
    } catch (error) {
      stream?.getTracks().forEach((track) => track.stop());
      const result = cameraError(error);
      setCameraState(result.state);
      setCameraMessage(result.message);
    }
  }, []);

  const stopCamera = useCallback(() => {
    streamRef.current?.getTracks().forEach((track) => track.stop());
    streamRef.current = null;
    capturedFrameRef.current = null;
    if (videoRef.current) videoRef.current.srcObject = null;
    setCaptured(false);
    setHasFrame(false);
    setCameraState("idle");
    setCameraMessage("Camera stopped. No frame has been uploaded or saved.");
  }, []);

  useEffect(() => () => {
    streamRef.current?.getTracks().forEach((track) => track.stop());
    cancelAnimationFrame(frameRequestRef.current);
  }, []);

  useEffect(() => {
    const renderFrame = () => {
      const canvas = canvasRef.current;
      const video = videoRef.current;
      if (canvas) {
        const source = capturedFrameRef.current
          ?? (video && video.readyState >= HTMLMediaElement.HAVE_CURRENT_DATA ? video : null);
        const width = source instanceof HTMLVideoElement ? source.videoWidth : source?.width ?? 0;
        const height = source instanceof HTMLVideoElement ? source.videoHeight : source?.height ?? 0;
        if (source && width > 0 && height > 0) {
          const scale = Math.min(1, 960 / width, 540 / height);
          const targetWidth = Math.max(1, Math.round(width * scale));
          const targetHeight = Math.max(1, Math.round(height * scale));
          if (canvas.width !== targetWidth || canvas.height !== targetHeight) {
            canvas.width = targetWidth;
            canvas.height = targetHeight;
          }
          const context = canvas.getContext("2d", { willReadFrequently: true });
          if (context) {
            context.drawImage(source, 0, 0, targetWidth, targetHeight);
            const input = context.getImageData(0, 0, targetWidth, targetHeight);
            context.putImageData(processImageData(input, effects), 0, 0);
            setHasFrame(true);
          }
        }
      }
      frameRequestRef.current = requestAnimationFrame(renderFrame);
    };
    frameRequestRef.current = requestAnimationFrame(renderFrame);
    return () => cancelAnimationFrame(frameRequestRef.current);
  }, [effects, cameraState, captured]);

  const capturePhoto = () => {
    const video = videoRef.current;
    if (!video || video.videoWidth === 0 || video.videoHeight === 0) return;
    const original = document.createElement("canvas");
    original.width = video.videoWidth;
    original.height = video.videoHeight;
    const context = original.getContext("2d");
    if (!context) {
      setCameraMessage("This browser could not create a photo canvas.");
      return;
    }
    context.drawImage(video, 0, 0);
    capturedFrameRef.current = original;
    setCaptured(true);
    setCameraMessage("Photo captured locally. Adjustments stay in this browser until you download it.");
  };

  const downloadPhoto = () => {
    const canvas = canvasRef.current;
    if (!canvas || !hasFrame) return;
    canvas.toBlob((blob) => {
      if (!blob) {
        setCameraMessage("The browser could not export this photo.");
        return;
      }
      const url = URL.createObjectURL(blob);
      const link = document.createElement("a");
      link.href = url;
      link.download = "camdoctor-photo.png";
      link.click();
      URL.revokeObjectURL(url);
    }, "image/png");
  };

  const testConnection = async () => {
    if (!apiKey.trim()) {
      setConnectionStatus("Enter a key to test");
      return;
    }
    setConnectionBusy(true);
    setConnectionStatus("Checking key and model…");
    setRequestError("");
    try {
      const result = await fetch("/api/groq-test", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        cache: "no-store",
        body: JSON.stringify({ apiKey, model }),
      });
      const data: { message?: string; error?: string } = await result.json();
      if (!result.ok) throw new Error(data.error ?? "Connection test failed.");
      setConnectionStatus(data.message ?? "Groq connection verified");
    } catch (error) {
      const message = error instanceof Error ? error.message : "Could not reach Groq.";
      setConnectionStatus(message);
      setRequestError(message);
    } finally {
      setConnectionBusy(false);
    }
  };

  const askDirector = async () => {
    const text = prompt.trim();
    if (!apiKey.trim()) {
      setRequestError("Add your Groq API key above to use AI Camera Director.");
      return;
    }
    if (!text || text.length > 1200) {
      setRequestError("Enter a request of 1–1200 characters.");
      return;
    }
    setChatBusy(true);
    setRequestError("");
    setProposal(null);
    setChatMessages((current) => [...current, { role: "user", text }]);
    setPrompt("");
    try {
      const result = await fetch("/api/director", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        cache: "no-store",
        body: JSON.stringify({ apiKey, model, prompt: text }),
      });
      const data: ChatResult | { error: string } = await result.json();
      if (!result.ok || "error" in data) {
        throw new Error("error" in data ? data.error : "AI request failed.");
      }
      const explanation = data.explanation || "Here is an optional adjustment for you to review.";
      setChatMessages((current) => [...current, { role: "assistant", text: explanation }]);
      setProposal(data.proposal);
      setConnectionStatus("Ready");
    } catch (error) {
      const message = error instanceof Error ? error.message : "Could not reach Groq.";
      setRequestError(message);
      setConnectionStatus(message);
      setChatMessages((current) => [...current, { role: "assistant", text: message }]);
    } finally {
      setChatBusy(false);
    }
  };

  const applyProposal = () => {
    if (!proposal) return;
    restoreEffects({ ...effects, ...proposal.effects });
    setProposal(null);
  };

  const undo = () => {
    const previous = historyRef.current.pop();
    if (previous) setEffects(previous);
  };

  const reset = () => restoreEffects(DEFAULT_EFFECTS);

  const setNumericEffect = (key: keyof ImageEffects) => (
    _event: ChangeEvent<HTMLInputElement>,
    data: { value: number },
  ) => updateEffect(key, data.value);

  return (
    <div className="app-shell">
      <header className="topbar">
        <a className="brand" href="#" aria-label="CamDoctor home">
          <span className="brand-mark"><VideoRegular /></span>
          <span>CamDoctor</span>
          <Badge appearance="tint" color="brand">ONLINE PREVIEW</Badge>
        </a>
        <nav className="top-links" aria-label="Project links">
          <a href={repoUrl} target="_blank" rel="noreferrer">GitHub</a>
          <a href={nativeBuildUrl} target="_blank" rel="noreferrer"><ArrowDownloadRegular /> Native app</a>
        </nav>
      </header>

      <main>
        <section className="hero">
          <div className="hero-copy">
            <Badge appearance="tint" color="brand">A clearer view of your camera</Badge>
            <Title1>Make every frame feel more like you.</Title1>
            <Text className="hero-description">
              Explore your webcam, tune a photo, and get thoughtful creative suggestions.
              No install. Your camera and images stay on your device.
            </Text>
            <div className="hero-actions">
              {cameraState === "active" ? (
                <Button appearance="primary" icon={<CameraRegular />} onClick={capturePhoto} disabled={!hasFrame}>
                  Capture photo
                </Button>
              ) : (
                <Button appearance="primary" icon={<VideoRegular />} onClick={startCamera}>
                  Try online
                </Button>
              )}
              {cameraState === "active" && (
                <Button appearance="secondary" onClick={stopCamera}>Turn camera off</Button>
              )}
              <span className="privacy-chip"><LockClosedRegular /> Camera access starts only when you choose</span>
            </div>
          </div>
          <div className="hero-art" aria-hidden="true">
            <div className="art-glow" />
            <div className="art-frame">
              <div className="art-top"><span /><span /><span /></div>
              <div className="art-image"><div className="art-sun" /><div className="art-hill hill-back" /><div className="art-hill hill-front" /></div>
              <div className="art-caption"><span>FRAME 01</span><strong>Find your light.</strong></div>
            </div>
            <div className="floating-stat"><SparkleRegular /><span><b>YOUR LOOK</b><br />Made by you</span></div>
          </div>
        </section>

        <section className="experience-grid" id="try">
          <Card className="preview-card">
            <CardHeader
              header={<div><Text className="eyebrow">01 / YOUR CAMERA</Text><Title2>Live preview</Title2></div>}
              action={<Badge appearance="tint" color={cameraState === "active" ? "success" : "informative"}>
                {cameraState === "active" ? "Camera on" : "Camera off"}
              </Badge>}
            />
            <div className={`preview-stage ${hasFrame ? "has-frame" : ""}`}>
              <canvas ref={canvasRef} aria-label={captured ? "Captured photo with selected effects" : "Live camera preview"} />
              {!hasFrame && (
                <div className="preview-empty">
                  <span className="camera-orb"><CameraRegular /></span>
                  <strong>Your view, your rules.</strong>
                  <p>{cameraMessage}</p>
                  {cameraState !== "active" && (
                    <Button appearance="primary" onClick={startCamera}>Allow camera &amp; begin</Button>
                  )}
                  {cameraState === "active" && <span className="permission-wait">Waiting for the first frame…</span>}
                </div>
              )}
              {hasFrame && (
                <div className="preview-overlay">
                  <Badge appearance="tint" color={captured ? "brand" : "success"}>
                    {captured ? "CAPTURED PHOTO" : "LIVE · LOCAL"}
                  </Badge>
                  <span>Browser processed</span>
                </div>
              )}
            </div>
            <div className="preview-footer">
              <Text>{cameraMessage}</Text>
              {captured && <Button appearance="subtle" icon={<ArrowDownloadRegular />} onClick={downloadPhoto}>Download photo</Button>}
            </div>
            <div className="camera-note"><LockClosedRegular /> No image is uploaded, recorded, or stored by CamDoctor.</div>
          </Card>

          <Card className="controls-card">
            <CardHeader
              header={<div><Text className="eyebrow">02 / MAKE IT YOURS</Text><Title2>Light &amp; color</Title2></div>}
              action={<Badge appearance="outline">LOCAL FILTERS</Badge>}
            />
            <div className="controls-body">
              <Checkbox
                checked={effects.grayscale}
                label="Black & white"
                onChange={(_, data) => updateEffect("grayscale", data.checked === true)}
              />
              <Field label={`Brightness · ${effects.brightness > 0 ? "+" : ""}${effects.brightness}`}>
                <Slider min={-100} max={100} step={1} value={effects.brightness}
                  onPointerDown={beginEdit} onPointerUp={endEdit} onBlur={endEdit}
                  onChange={setNumericEffect("brightness")} />
              </Field>
              <Field label={`Contrast · ${effects.contrast}%`}>
                <Slider min={50} max={150} step={1} value={effects.contrast}
                  onPointerDown={beginEdit} onPointerUp={endEdit} onBlur={endEdit}
                  onChange={setNumericEffect("contrast")} />
              </Field>
              <Field label={`Saturation · ${effects.saturation}%`}>
                <Slider min={0} max={200} step={1} value={effects.saturation}
                  onPointerDown={beginEdit} onPointerUp={endEdit} onBlur={endEdit}
                  onChange={setNumericEffect("saturation")} />
              </Field>
              <Field label={`Warm ↔ cool · ${effects.warmth > 0 ? "+" : ""}${effects.warmth}`}>
                <Slider min={-100} max={100} step={1} value={effects.warmth}
                  onPointerDown={beginEdit} onPointerUp={endEdit} onBlur={endEdit}
                  onChange={setNumericEffect("warmth")} />
              </Field>
              <div className="control-actions">
                <Button icon={<ArrowUndoRegular />} onClick={undo} disabled={historyRef.current.length === 0}>Undo</Button>
                <Button icon={<ArrowResetRegular />} onClick={reset}>Reset</Button>
              </div>
              <div className="effect-hint">
                <SparkleRegular />
                <span>Effects are browser-only and apply to the preview and downloaded image.</span>
              </div>
            </div>
          </Card>
        </section>

        <section className="director-section">
          <div className="section-intro">
            <Text className="eyebrow">03 / A CREATIVE SECOND OPINION</Text>
            <Title2>AI Camera Director</Title2>
            <Text>Ask for a style. Review a suggestion. You stay in control.</Text>
          </div>
          <div className="director-grid">
            <Card className="ai-settings-card">
              <CardHeader header={<div><Text className="eyebrow">BRING YOUR OWN KEY</Text><Title2>Your Groq connection</Title2></div>} />
              <div className="ai-settings-body">
                <Text>Your key is held in page memory and forwarded transiently to Groq through this site's server endpoint. It is not saved. Clear it or close this tab to remove it.</Text>
                <Field label="Groq API key" hint="Create or manage your key at console.groq.com/keys">
                  <Input
                    type={showKey ? "text" : "password"}
                    autoComplete="off"
                    value={apiKey}
                    onChange={(_, data) => {
                      setApiKey(data.value);
                      setConnectionStatus("Key stays in memory only");
                    }}
                    contentAfter={
                      <Button appearance="subtle" size="small" onClick={() => setShowKey((value) => !value)}>
                        {showKey ? "Hide" : "Show"}
                      </Button>
                    }
                    placeholder="Paste your Groq key"
                  />
                </Field>
                <Field label="Groq model">
                  <Select value={model} onChange={(event) => setModel(event.target.value)}>
                    {GROQ_MODELS.map((item) => <option key={item.id} value={item.id}>{item.label}</option>)}
                  </Select>
                </Field>
                <div className="connection-row">
                  <Button appearance="secondary" onClick={testConnection} disabled={connectionBusy || !apiKey.trim()}>
                    {connectionBusy ? "Testing…" : "Test connection"}
                  </Button>
                  <Badge appearance="tint" color={connectionStatus === "Ready" || connectionStatus.includes("verified") ? "success" : "informative"}>
                    {connectionStatus}
                  </Badge>
                </div>
                <a className="keys-link" href="https://console.groq.com/keys" target="_blank" rel="noreferrer">
                  Get a Groq API key <span aria-hidden="true">↗</span>
                </a>
                <div className="privacy-callout"><LockClosedRegular /><span>Requests contain only your text prompt. No camera feed, photo, local file, or image metrics are sent.</span></div>
              </div>
            </Card>

            <Card className="chat-card">
              <CardHeader
                header={<div><Text className="eyebrow">A HUMAN-IN-THE-LOOP ASSISTANT</Text><Title2>What would you like to create?</Title2></div>}
                action={<SparkleRegular />}
              />
              <div className="chat-body">
                {chatMessages.length === 0 ? (
                  <div className="chat-empty">
                    <span className="sparkle-disc"><SparkleRegular /></span>
                    <strong>Start with a feeling, not a setting.</strong>
                    <p>Try “Give this a warm, cinematic feel” or “Make it black and white.”</p>
                  </div>
                ) : (
                  <div className="messages" aria-live="polite">
                    {chatMessages.map((message, index) => (
                      <div className={`message ${message.role}`} key={`${message.role}-${index}`}>
                        <span>{message.role === "user" ? "YOU" : "CAMDOCTOR"}</span>
                        <p>{message.text}</p>
                      </div>
                    ))}
                    {chatBusy && <div className="thinking"><span /> Thinking through your request…</div>}
                  </div>
                )}
                {proposal && (
                  <div className="proposal">
                    <div className="proposal-heading"><SparkleRegular /><strong>Suggested adjustments</strong><Badge appearance="tint">REVIEW FIRST</Badge></div>
                    <ul>
                      {Object.entries(proposal.effects).map(([name, value]) => (
                        <li key={name}><span>{name}</span><b>{typeof value === "boolean" ? (value ? "On" : "Off") : value}</b></li>
                      ))}
                    </ul>
                    <div className="proposal-actions">
                      <Button appearance="primary" onClick={applyProposal}>Apply suggestion</Button>
                      <Button appearance="subtle" onClick={() => setProposal(null)}>Dismiss</Button>
                    </div>
                  </div>
                )}
                <div className="chat-input">
                  <Textarea
                    value={prompt}
                    onChange={(_, data) => setPrompt(data.value)}
                    onKeyDown={(event) => {
                      if (event.key === "Enter" && !event.shiftKey) {
                        event.preventDefault();
                        void askDirector();
                      }
                    }}
                    placeholder="Describe the look you have in mind…"
                    resize="vertical"
                    maxLength={1200}
                  />
                  <div className="send-row">
                    <Text>{prompt.length}/1200 · Groq receives text only</Text>
                    <Button appearance="primary" icon={<SparkleRegular />} onClick={() => void askDirector()} disabled={chatBusy || !prompt.trim()}>
                      {chatBusy ? "Thinking…" : "Ask Director"}
                    </Button>
                  </div>
                </div>
                {requestError && <Text className="error-text" role="alert">{requestError}</Text>}
              </div>
            </Card>
          </div>
        </section>

        <section className="platform-section">
          <div>
            <Text className="eyebrow">TWO WAYS TO CARE FOR YOUR CAMERA</Text>
            <Title2>Browser experience. Native control.</Title2>
          </div>
          <div className="platform-grid">
            <Card className="platform-card online">
              <Badge appearance="tint" color="brand">YOU ARE HERE</Badge>
              <h3>CamDoctor Online</h3>
              <p>Webcam preview, photo capture, and software color filters. Works in your browser, with no install. It cannot change hardware settings.</p>
              <span><VideoRegular /> Browser · getUserMedia · Canvas</span>
            </Card>
            <Card className="platform-card native">
              <Badge appearance="tint" color="informative">NATIVE WINDOWS APP</Badge>
              <h3>CamDoctor for Windows</h3>
              <p>Native camera controls exposed by your device, local diagnostics, profiles, and a direct hardware preview through Qt.</p>
              <a href={nativeBuildUrl} target="_blank" rel="noreferrer">Build from source ↗</a>
            </Card>
          </div>
        </section>
      </main>
      <footer>
        <a className="brand footer-brand" href={repoUrl} target="_blank" rel="noreferrer"><span className="brand-mark"><VideoRegular /></span>CamDoctor</a>
        <span>Camera access is opt-in. Images stay on your device.</span>
        <a href={repoUrl} target="_blank" rel="noreferrer">Source on GitHub</a>
      </footer>
      <video ref={videoRef} className="camera-source" autoPlay muted playsInline />
    </div>
  );
}

export default App;
