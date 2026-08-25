// LoggerContext.jsx
import React, {createContext, useContext, useEffect, useRef, useState, useCallback, useMemo} from 'react';

const LoggerContext = createContext(null);

export const LoggerProvider = ({ children }) => {
    const [logList, setLogList] = useState([]);
    const channelRef = useRef(null);

    // ✅ 【生命周期挂载】创建通道、绑定监听
    useEffect(() => {
        const channel = new BroadcastChannel('log');
        channelRef.current = channel;

        const handleMessage = (event) => {
            console.log('LoggerProvider: 接收到日志', event.data);
            // Keep only the most recent 300 entries (matches the UI hint).
            setLogList((prev) => [...prev, event.data].slice(-300));
        };
        channel.addEventListener('message', handleMessage);

        // ✅ 【生命周期卸载】清理通道
        return () => {
            channel.removeEventListener('message', handleMessage);
            channel.close();
            channelRef.current = null;
            console.log('LoggerProvider: 通道已关闭');
        };
    }, []);

    const log = useCallback((level, tag, message) => {
        if (!channelRef.current) {
            console.warn('日志通道已关闭');
            return;
        }
        channelRef.current.postMessage({ level, tag, message });
    }, []);

    const logInfo = useCallback((tag, msg) => log('info', tag, msg), [log]);
    const logError = useCallback((tag, msg) => log('error', tag, msg), [log]);
    const logWarning = useCallback((tag, msg) => log('warning', tag, msg), [log]);
    const logDebug = useCallback((tag, msg) => log('debug', tag, msg), [log]);

    const value = useMemo(() => ({
        logList,
        logInfo,
        logError,
        logWarning,
        logDebug,
        clearLogs: () => setLogList([]),
    }), [logList, logInfo, logError, logWarning, logDebug]);

    return (
        <LoggerContext.Provider value={value}>
            {children}
        </LoggerContext.Provider>
    );
};

export const useLogger = () => {
    const context = useContext(LoggerContext);
    if (!context) {
        throw new Error('useLogger 必须在 LoggerProvider 内部使用');
    }
    return context;
};