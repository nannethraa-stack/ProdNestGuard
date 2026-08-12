require('dotenv').config();
const express = require('express');
const http = require('http');
const { Server } = require('socket.io');
const Database = require('better-sqlite3');
const path = require('path');
const crypto = require('crypto');
const helmet = require('helmet');
const cors = require('cors');
const rateLimit = require('express-rate-limit');

const app = express();
const server = http.createServer(app);

// Socket.io with strict CORS configuration
const io = new Server(server, {
    cors: {
        origin: process.env.DASHBOARD_ORIGINS ? process.env.DASHBOARD_ORIGINS.split(',') : ['http://localhost:5000'],
        methods: ["GET", "POST"]
    }
});

const PORT = process.env.PORT || 5000;
const DEVICE_API_KEY = process.env.DEVICE_API_KEY || 'default-insecure-dev-key';

// Security Headers Middleware
app.use(helmet({
    contentSecurityPolicy: {
        directives: {
            defaultSrc: ["'self'"],
            scriptSrc: ["'self'", "https://cdn.jsdelivr.net"],
            styleSrc: ["'self'", "https://cdn.jsdelivr.net", "'unsafe-inline'"],
            connectSrc: ["'self'"],
        }
    }
}));

// CORS Middleware for Express routes
app.use(cors({
    origin: process.env.DASHBOARD_ORIGINS ? process.env.DASHBOARD_ORIGINS.split(',') : ['http://localhost:5000']
}));

app.use(express.json());
app.use(express.static(path.join(__dirname, 'public')));

// Rate limiter for telemetry ingestion (120 req/min)
const telemetryLimiter = rateLimit({
    windowMs: 60 * 1000,
    max: 120,
    standardHeaders: true,
    legacyHeaders: false,
    message: { success: false, error: 'Rate limit exceeded.' }
});

// Initialize SQLite Database using synchronous better-sqlite3
const dbPath = path.join(__dirname, 'database.sqlite');
const db = new Database(dbPath);

// Create tables and performance indexes
db.exec(`
    CREATE TABLE IF NOT EXISTS telemetry (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        device_id TEXT NOT NULL,
        event_type TEXT NOT NULL,
        confidence REAL,
        max_temp REAL,
        ammonia_ppm REAL,
        diaper_status TEXT,
        probabilistic_diagnosis TEXT,
        timestamp DATETIME DEFAULT CURRENT_TIMESTAMP
    );
    CREATE INDEX IF NOT EXISTS idx_telemetry_device_timestamp ON telemetry(device_id, timestamp);
`);

// Allow-list for valid event types
const VALID_EVENT_TYPES = [
    'FACE_CLEAR', 'FACE_PARTIALLY_COVERED', 'FACE_DOWN_CRITICAL', 
    'FACE_OBSTRUCTION_CRITICAL', 'AUDIO_CRY_PAIN', 'AUDIO_CRY_HUNGER', 
    'AUDIO_CRY_DISCOMFORT', 'DIAPER_SOILED_ALERT', 'MONITORING', 'NORMAL'
];

// Device Authentication Middleware
function authenticateDevice(req, res, next) {
    const headerKey = req.headers['x-device-key'];
    if (!headerKey || typeof headerKey !== 'string') {
        return res.status(401).json({ success: false, error: 'Unauthorized: Missing device key' });
    }
    const providedBuffer = Buffer.from(headerKey);
    const expectedBuffer = Buffer.from(DEVICE_API_KEY);
    
    if (providedBuffer.length !== expectedBuffer.length || !crypto.timingSafeEqual(providedBuffer, expectedBuffer)) {
        return res.status(403).json({ success: false, error: 'Forbidden: Invalid device key' });
    }
    next();
}

// Telemetry Ingestion Endpoint
app.post('/api/telemetry', telemetryLimiter, authenticateDevice, (req, res) => {
    const { device_id, event_type, confidence, max_temp, ammonia_ppm, diaper_status, probabilistic_diagnosis } = req.body;

    // Strict Input Validation
    if (!device_id || typeof device_id !== 'string' || !/^[a-zA-Z0-9_-]{3,32}$/.test(device_id)) {
        return res.status(400).json({ success: false, error: 'Invalid or missing device_id format' });
    }
    if (!event_type || !VALID_EVENT_TYPES.includes(event_type)) {
        return res.status(400).json({ success: false, error: 'Invalid or unallowed event_type' });
    }
    if (confidence !== undefined && (typeof confidence !== 'number' || confidence < 0 || confidence > 1)) {
        return res.status(400).json({ success: false, error: 'Confidence must be a number between 0 and 1' });
    }
    if (max_temp !== undefined && (typeof max_temp !== 'number' || max_temp < 20 || max_temp > 50)) {
        return res.status(400).json({ success: false, error: 'max_temp out of safe biological range' });
    }
    if (ammonia_ppm !== undefined && (typeof ammonia_ppm !== 'number' || ammonia_ppm < 0 || ammonia_ppm > 100)) {
        return res.status(400).json({ success: false, error: 'ammonia_ppm out of valid range' });
    }

    try {
        const stmt = db.prepare(`INSERT INTO telemetry 
            (device_id, event_type, confidence, max_temp, ammonia_ppm, diaper_status, probabilistic_diagnosis) 
            VALUES (?, ?, ?, ?, ?, ?, ?)`);

        const info = stmt.run(
            device_id,
            event_type,
            confidence ?? 0.0,
            max_temp ?? 0.0,
            ammonia_ppm ?? 0.0,
            diaper_status || 'NORMAL',
            probabilistic_diagnosis || 'Normal State'
        );

        const payload = {
            id: info.lastInsertRowid,
            device_id,
            event_type,
            confidence: confidence ?? 0.0,
            max_temp: max_temp ?? 0.0,
            ammonia_ppm: ammonia_ppm ?? 0.0,
            diaper_status: diaper_status || 'NORMAL',
            probabilistic_diagnosis: probabilistic_diagnosis || 'Normal State',
            timestamp: new Date().toISOString()
        };

        // Scoped Room & Global Broadcast via WebSockets
        io.to(`device_${device_id}`).emit('telemetry_update', payload);
        io.to('all_devices').emit('telemetry_update', payload);

        res.status(200).json({ success: true, status: 'RECEIVED', id: info.lastInsertRowid });
    } catch (err) {
        console.error('Database write error:', err.message);
        res.status(500).json({ success: false, error: 'Internal server error during ingestion' });
    }
});

// GET History Endpoint
app.get('/api/telemetry', (req, res) => {
    const limit = parseInt(req.query.limit, 10) || 50;
    const deviceId = req.query.device_id;

    try {
        let stmt;
        if (deviceId) {
            stmt = db.prepare(`SELECT * FROM telemetry WHERE device_id = ? ORDER BY timestamp DESC LIMIT ?`);
            res.json(stmt.all(deviceId, limit));
        } else {
            stmt = db.prepare(`SELECT * FROM telemetry ORDER BY timestamp DESC LIMIT ?`);
            res.json(stmt.all(limit));
        }
    } catch (err) {
        console.error('History query error:', err.message);
        res.status(500).json({ success: false, error: 'Failed to retrieve telemetry history' });
    }
});

// Health check endpoint
app.get('/api/health', (req, res) => {
    res.status(200).json({ status: 'HEALTHY', uptime: process.uptime() });
});

// Socket.io Connection & Room Subscriptions
io.on('connection', (socket) => {
    console.log(`Dashboard client connected: ${socket.id}`);

    socket.on('subscribe', (room) => {
        socket.join(room);
        console.log(`Client ${socket.id} subscribed to room: ${room}`);
    });

    socket.on('disconnect', () => {
        console.log(`Dashboard client disconnected: ${socket.id}`);
    });
});

// Graceful Shutdown Handler
function shutdown() {
    console.log('Shutting down server gracefully...');
    server.close(() => {
        db.close();
        console.log('Database connection and HTTP server closed.');
        process.exit(0);
    });
    setTimeout(() => {
        console.error('Forced shutdown due to timeout.');
        process.exit(1);
    }, 5000);
}

process.on('SIGTERM', shutdown);
process.on('SIGINT', shutdown);

// Export for testing or standalone execution
if (require.main === module) {
    server.listen(PORT, () => {
        console.log(`NestGuard Production Server running on port ${PORT}`);
    });
}

module.exports = { app, server, db };
