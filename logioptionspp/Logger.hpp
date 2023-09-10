#ifndef __LOGGER__
#define __LOGGER__

#include <Windows.h>
#include <mutex>
#include <fstream>

namespace utils
{
    std::string convertWideToUTF8(const std::wstring& wideString)
    {
        int requiredSize = WideCharToMultiByte(CP_UTF8, 0, wideString.c_str(), -1, nullptr, 0, nullptr, nullptr);
        if (requiredSize == 0) {
            // Handle error
            return "";
        }

        std::string utf8String(requiredSize, '\0');
        if (WideCharToMultiByte(CP_UTF8, 0, wideString.c_str(), -1, &utf8String[0], requiredSize, nullptr, nullptr) == 0) {
            // Handle error
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
            // Close the log file if open
            if (m_logFile.is_open())
            {
                m_logFile.close();
            }
        }

        // Declare private copy constructor and assignment operator to prevent copies
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
            std::lock_guard<std::mutex> lock(m_mutex); // Ensure thread safety
            m_logFilePath = convertWideToUTF8(logFilePath);
        }

        void setLogFilePath(const std::string& logFilePath)
        {
            std::lock_guard<std::mutex> lock(m_mutex); // Ensure thread safety
            m_logFilePath = logFilePath;
        }

        void log(const std::wstring& message)
        {
            if (!m_isEnabled) { return; }
            log(convertWideToUTF8(message));
        }

        void log(const std::string& message)
        {
            if (!m_isEnabled) { return; }
            std::lock_guard<std::mutex> lock(m_mutex); // Ensure thread safety

            if (!m_logFile.is_open())
            {
                // TODO: remove hardcoded path
                m_logFile.open(m_logFilePath, std::ios::app);
            }

            if (m_logFile.is_open())
            {
                m_logFile << message << std::endl;
                m_logFile.close();
            }
        }

        void enable() {
            m_isEnabled = true;
        }

        void disable() {
            m_isEnabled = false;
        }

    private:
        std::ofstream m_logFile;
        std::mutex m_mutex;
        std::string m_logFilePath;
        bool m_isEnabled{ false };
    };
}

#endif // __LOGGER__