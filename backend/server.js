require('dotenv').config();
const express = require('express');
const http = require('http');
const { Server } = require('socket.io');
const cors = require('cors');
const sqlite3 = require('sqlite3').verbose();
const path = require('path');

const app = express();
const server = http.createServer(app);
const io = new Server(server, {
    cors: { origin: "*" }
});

app.use(express.json());
app.use(cors());

// Serve static frontend dashboard from the 'public' folder
app.use(express.static(path.join(__dirname, 'public')));

// Initialize SQLite Database for Event Logs
const db = new sqlite3.Database('./nestguard.db', (err) => {
    if (err) console.error('Database connection error:', err.message);
    else console.log('Connected to local SQLite database.');
});

db.run(`CREATE TABLE IF NOT EXISTS events (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    device_id TEXT,
    event_type TEXT,
    confidence REAL,
    max_temp REAL,
    timestamp DATETIME DEFAULT CURRENT_TIMESTAMP
)`);

// Endpoint for Portenta H7 to push thermal and AI inference telemetry
app.post('/api/telemetry', (req, res) => {
    const { device_id, event_type, confidence, max_temp } = req.body;

    if (!device_id) {
        return res.status(400).json({ error: 'Missing required device_id field' });
    }

    const resolvedEventType = event_type || 'thermal_monitoring';
    const resolvedConfidence = confidence || 0.0;
    const resolvedMaxTemp = max_temp !== undefined ? max_temp : null;

    const query = `INSERT INTO events (device_id, event_type, confidence, max_temp) VALUES (?, ?, ?, ?)`;
    db.run(query, [device_id, resolvedEventType, resolvedConfidence, resolvedMaxTemp], function(err) {
        if (err) {
            return res.status(500).json({ error: err.message });
        }

        // Broadcast real-time event to connected dashboards via WebSockets
        io.emit('live_alert', {
            id: this.lastID,
            device_id,
            event_type: resolvedEventType,
            confidence: resolvedConfidence,
            max_temp: resolvedMaxTemp,
            timestamp: new Date().toISOString()
        });

        console.log(`[TELEMETRY] Device ${device_id} -> Max Temp: ${resolvedMaxTemp}°C | Event: ${resolvedEventType}`);
        return res.status(200).json({ status: 'success', event_id: this.lastID });
    });
});

// Endpoint to fetch recent logs for dashboard history
app.get('/api/events', (req, res) => {
    db.all(`SELECT * FROM events ORDER BY timestamp DESC LIMIT 50`, [], (err, rows) => {
        if (err) return res.status(500).json({ error: err.message });
        res.json(rows);
    });
});

// WebSocket connection handler
io.on('connection', (socket) => {
    console.log('Dashboard client connected:', socket.id);
    socket.on('disconnect', () => {
        console.log('Dashboard client disconnected:', socket.id);
    });
});

const PORT = process.env.PORT || 5000;
server.listen(PORT, () => {
    console.log(`NestGuard Backend Server running on port ${PORT}`);
});