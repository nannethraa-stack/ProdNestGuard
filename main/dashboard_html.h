#ifndef DASHBOARD_HTML_H
#define DASHBOARD_HTML_H

const char dashboard_html[] = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>ProdNestGuard - Local Dashboard</title>
    <style>
        body { font-family: Arial, sans-serif; background: #f4f6f9; text-align: center; margin: 0; padding: 20px; }
        .card { background: white; max-width: 500px; margin: 20px auto; padding: 20px; border-radius: 8px; box-shadow: 0 4px 8px rgba(0,0,0,0.1); }
        .status-ok { color: #28a745; font-weight: bold; }
        .status-alert { color: #dc3545; font-weight: bold; animation: blink 1s infinite; }
        @keyframes blink { 0% { opacity: 1; } 50% { opacity: 0.3; } 100% { opacity: 1; } }
        img { width: 100%; border-radius: 4px; margin-top: 10px; background: #000; height: 300px; }
    </style>
</head>
<body>
    <h1>ProdNestGuard Monitor</h1>
    <div class="card">
        <h3>System Status</h3>
        <p>Audio (Cry Detection): <span id="audio-status" class="status-ok">Monitoring...</span></p>
        <p>Vision (Obstruction): <span id="vision-status" class="status-ok">Monitoring...</span></p>
    </div>
    <div class="card">
        <h3>Live Camera Stream</h3>
        <img src="/stream" alt="Camera Stream Offline">
    </div>
</body>
</html>
)rawliteral";

#endif // DASHBOARD_HTML_H
