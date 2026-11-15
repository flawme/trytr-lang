const mathmlNamespace = "http://www.w3.org/1998/Math/MathML";
    const allowedMathml = new Set([
      "math", "mtable", "mtr", "mtd", "mrow", "mi", "mn", "mo", "mtext",
      "mfrac", "msqrt", "msub", "msup", "msubsup"
    ]);
    const messages = document.getElementById("messages");
    const form = document.getElementById("ask-form");
    const sendButton = document.getElementById("send");
    const promptInput = document.getElementById("prompt");

    function sanitizeMathml(source) {
      const parsed = new DOMParser().parseFromString(source, "application/xml");
      const root = parsed.documentElement;
      if (parsed.querySelector("parsererror") || root.localName !== "math" ||
          root.namespaceURI !== mathmlNamespace) return null;
      function copy(node) {
        if (node.nodeType === Node.TEXT_NODE) return document.createTextNode(node.nodeValue || "");
        if (node.nodeType !== Node.ELEMENT_NODE || node.namespaceURI !== mathmlNamespace ||
            !allowedMathml.has(node.localName)) return null;
        const result = document.createElementNS(mathmlNamespace, node.localName);
        if (node.localName === "math" && node.getAttribute("display") === "block") {
          result.setAttribute("display", "block");
        }
        for (const child of node.childNodes) {
          const safe = copy(child);
          if (safe === null) return null;
          result.append(safe);
        }
        return result;
      }
      return copy(root);
    }

    function showMathml(target, source) {
      target.replaceChildren();
      const math = sanitizeMathml(source);
      if (!math) {
        target.textContent = "Trytr could not render this equation.";
        return false;
      }
      target.append(math);
      return true;
    }

    async function loadExample() {
      const result = await fetch("/api/render", {
        method: "POST",
        headers: {"Content-Type": "application/json"},
        body: JSON.stringify({expressions: [
          "a + 0 = a",
          "a + S(b) = S(a + b)",
          "S(0) + S(0) = S(S(0) + 0)",
          "S(S(0) + 0) = S(S(0))",
          "S(S(0)) = 2"
        ]})
      });
      const payload = await result.json();
      if (!result.ok) throw new Error(payload.error || "Trytr rendering failed.");
      showMathml(document.getElementById("sample-math"), payload.mathml);
    }

    function addMessage(kind, label, text = "") {
      const article = document.createElement("article");
      article.className = `message ${kind}`;
      const title = document.createElement("span");
      title.className = "label";
      title.textContent = label;
      const body = document.createElement("p");
      body.textContent = text;
      article.append(title, body);
      messages.append(article);
      messages.scrollTop = messages.scrollHeight;
      return {article, body};
    }

    async function readEvents(response, onEvent) {
      const reader = response.body.getReader();
      const decoder = new TextDecoder();
      let buffer = "";
      while (true) {
        const {value, done} = await reader.read();
        if (done) break;
        buffer += decoder.decode(value, {stream: true});
        let boundary;
        while ((boundary = buffer.indexOf("\n\n")) >= 0) {
          const block = buffer.slice(0, boundary);
          buffer = buffer.slice(boundary + 2);
          let name = "message";
          let data = "";
          for (const line of block.split(/\r?\n/)) {
            if (line.startsWith("event: ")) name = line.slice(7);
            if (line.startsWith("data: ")) data += line.slice(6);
          }
          try { onEvent(name, JSON.parse(data)); } catch (_) { /* ignore malformed stream frames */ }
        }
      }
    }

    form.addEventListener("submit", async (event) => {
      event.preventDefault();
      const prompt = promptInput.value.trim();
      const apiKey = document.getElementById("api-key").value.trim();
      const model = document.getElementById("model").value.trim();
      if (!prompt || !apiKey || !model) {
        addMessage("assistant", "Setup needed", "Enter your OpenRouter key and model, then ask a question.");
        return;
      }
      addMessage("user", "You", prompt);
      const assistant = addMessage("assistant", "Trytr assistant", "");
      const mathBox = document.createElement("div");
      mathBox.className = "math";
      assistant.article.append(mathBox);
      sendButton.disabled = true;
      try {
        const response = await fetch("/api/chat", {
          method: "POST",
          headers: {"Content-Type": "application/json"},
          body: JSON.stringify({apiKey, model, prompt})
        });
        if (!response.ok) {
          const payload = await response.json();
          assistant.body.textContent = payload.error || "The request was rejected.";
          mathBox.remove();
          return;
        }
        let raw = "";
        await readEvents(response, (name, payload) => {
          if (name === "delta") {
            raw += payload.text || "";
            const marker = "\nMATH:";
            const markerAt = raw.indexOf(marker);
            assistant.body.textContent = markerAt >= 0
              ? raw.slice(0, markerAt).trimStart()
              : raw.slice(0, Math.max(0, raw.length - marker.length));
          } else if (name === "error") {
            assistant.body.textContent = payload.message || "Request failed.";
            mathBox.remove();
          } else if (name === "done") {
            assistant.body.textContent = payload.text || "The model returned no explanation.";
            if (payload.mathml) showMathml(mathBox, payload.mathml);
            else mathBox.remove();
            if (payload.notice) {
              const note = document.createElement("p");
              note.className = "notice";
              note.textContent = payload.notice;
              assistant.article.append(note);
            }
          }
          messages.scrollTop = messages.scrollHeight;
        });
      } catch (_) {
        assistant.body.textContent = "Could not reach the local Trytr demo server.";
        mathBox.remove();
      } finally {
        sendButton.disabled = false;
      }
    });

    loadExample().catch((error) => {
      document.getElementById("sample-math").textContent = error.message;
    });
