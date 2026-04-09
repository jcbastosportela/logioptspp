#ifndef LOGGER_HPP
#define LOGGER_HPP

#include <Windows.h>
#include <atomic>
#include <mutex>
#include <fstream>
#include <string>

namespace utils
{
    inline std::string convertWideToUTF8(const std::wstring& wideString)
    {
        if (wideString.empty()) {
            return "";
        }

        int requiredSize = WideCharToMultiByte(CP_UTF8, 0, wideString.c_str(),
            static_cast<int>(wideString.size()), nullptr, 0, nullptr, nullptr);
        if (requiredSize == 0) {
            return "";
        }

        std::string utf8String(requiredSize, '\0');
        if (WideCharToMultiByte(CP_UTF8, 0, wideString.c_str(),
            static_cast<int>(wideString.size()), &utf8String[0], requiredSize, nullptr, nullptr) == 0) {
            return "";
        }

        return utf8String;
    }

    class Logger
    {
    private:
        Logger() = default;
        ~Logger()
        {
            if (m_logFile.is_open())
            {
                m_logFile.close();
            }
        }

        Logger(const Logger&) = delete;
        Logger& operator=(const Logger&) = delete;

    public:
        static Logger& getInstance()
        {
            static Logger instance;
            return instance;
        }

        void setLogFilePath(const std::wstring& logFilePath)
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_logFilePath = convertWideToUTF8(logFilePath);
        }

        void setLogFilePath(const std::string& logFilePath)
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_logFilePath = logFilePath;
        }

        void log(const std::wstring& message)
        {
            if (!m_isEnabled.load(std::memory_order_relaxed)) { return; }
            log(convertWideToUTF8(message));
        }

        void log(const std::string& message)
        {
            if (!m_isEnabled.load(std::memory_order_relaxed)) { return; }
            std::lock_guard<std::mutex> lock(m_mutex);

            if (!m_logFile.is_open())
            {
                m_logFile.open(m_logFilePath, std::ios::app);
            }

            if (m_logFile.is_open())
            {
                m_logFile << message << std::endl;
                m_logFile.close();
            }
        }

        void enable() {
            m_isEnabled.store(true, std::memory_order_relaxed);
        }

        void disable() {
            m_isEnabled.store(false, std::memory_order_relaxed);
        }

    private:
        std::ofstream m_logFile;
        std::mutex m_mutex;
        std::string m_logFilePath;
        std::atomic<bool> m_isEnabled{ false };
    };
}

#endif // LOGGER_HPP
