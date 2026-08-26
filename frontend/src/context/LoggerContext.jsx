// LoggerContext.jsx
import React, {createContext, useContext, useEffect, useRef, useState, useCallback, useMemo} from 'react';
import { call } from '../bridge';

const LoggerContext = createContext(null);

export const LoggerProvider = ({ children }) => {
    const [logList, setLogList] = useState([]);
    const channelRef = useRef(null);

    // ✅ 【生命周期挂载】先读日志文件历史（今天 log-*.txt 的尾部），再创建
    // 通道收实时日志 —— 页面（重新）加载后控制台不再是空的：后台/托盘期间
    // 发生的日志（WebView 已销毁，实时通道收不到）会先从文件补回来。
    useEffect(() => {
        const channel = new BroadcastChannel('log');
        channelRef.current = channel;
        const early = [];        // 历史加载完成前到达的实时条目
        let historyLoaded = false;

        const handleMessage = (event) => {
            console.log('LoggerProvider: 接收到日志', event.data);
            if (!historyLoaded) early.push(event.data);
            // Keep only the most recent 300 entries (matches the UI hint).
            setLogList((prev) => [...prev, event.data].slice(-300));
        };
        channel.addEventListener('message', handleMessage);

        call('log_history')
            .then((entries) => {
                historyLoaded = true;
                if (Array.isArray(entries) && entries.length) {
                    // 文件尾部可能与已实时收到的最后一条重复（同一日志两路都到），
                    // 去重一下再和 early 合并，保持时间顺序（历史在前）。
                    const tail = entries.slice(-300);
                    const seen = new Set(tail.slice(-10).map((e) => `${e.time}|${e.tag}|${e.message}`));
                    const live = early.filter((e) => !seen.has(`${e.time}|${e.tag}|${e.message}`));
                    setLogList([...tail, ...live].slice(-300));
                } else if (early.length) {
                    setLogList(early.slice(-300));
                }
            })
            .catch((e) => {
                historyLoaded = true;
                console.error('[log_history]', e);
            });

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