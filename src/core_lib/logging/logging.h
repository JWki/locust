#pragma once

#include "../int_types.h"


namespace lc
{
    

    namespace logging
    {
        enum class LogLevel : uint16_t
        {
            LOG_LEVEL_TRACE = 0x00, 
            LOG_LEVEL_DEBUG = 0x01, 
            LOG_LEVEL_INFO = 0x02, 
            LOG_LEVEL_WARNING = 0x04, 
            LOG_LEVEL_ERROR = 0x08,  
            LOG_LEVEL_FATAL = 0x10
        };

      
        // djb2 hashing
        static size_t HashString(const char *str)
        {
            size_t hash = 5381;
            int c;

            while (c = *str++) {
                hash = ((hash << 5) + hash) + c; /* hash * 33 + c */
                return hash;
            }
            return -1;
        }

        struct LogChannel
        {
            const char* str = nullptr;
            size_t hash = 0;
            LogChannel() = default;
            LogChannel(const char* asStr)
                : str(asStr), hash(HashString(asStr)) {}
        };
        typedef uint32_t LogVerbosity;  

        class LoggerBase
        {
            void Register(); 
            void Unregister();
            LoggerBase* m_next = nullptr;
            LoggerBase* m_prev = nullptr;

        public:
            LoggerBase();
            virtual ~LoggerBase();

            virtual void Log(LogChannel channel, LogLevel level, LogVerbosity verbosity, SourceInfo scInfo, const char* format, va_list args) = 0;
       
            inline LoggerBase* GetNext() { return m_next; }
        };


        struct LogCriteria
        {
            LogChannel channel;
            LogLevel level;
            LogVerbosity verbosity;
            SourceInfo scInfo;
        };


        template <class FilterPolicy, class FormatPolicy, class WritePolicy>
        class Logger : public LoggerBase
        {
        protected:
            FilterPolicy    m_filter;
            FormatPolicy    m_formatter;
            WritePolicy     m_writer;

            char            m_buffer[512];
        public:
            virtual ~Logger() = default;
 
            virtual void Log(LogChannel channel, LogLevel level, LogVerbosity verbosity, SourceInfo scInfo, const char* format, va_list args) override
            {
                LogCriteria filterCriteria = { channel, level, verbosity, scInfo };
                if (m_filter.Filter(filterCriteria)) {
                    m_formatter.Format(m_buffer, 512, filterCriteria, format, args);
                    m_writer.Write(m_buffer);
                }
            }
        };

        void Log(LogChannel channel, LogLevel level, LogVerbosity verbosity, SourceInfo scInfo, const char* format, ...);
    }
}


#define GT_INFO(channelAsString, format, ...) \
lc::logging::Log(channelAsString, lc::logging::LogLevel::LOG_LEVEL_INFO, 0, GT_SOURCE_INFO, format, __VA_ARGS__)

#define GT_DEBUG(channelAsString, format, ...) \
lc::logging::Log(channelAsString, lc::logging::LogLevel::LOG_LEVEL_DEBUG, 0, GT_SOURCE_INFO, format, __VA_ARGS__)

#define GT_WARNING(channelAsString, format, ...) \
lc::logging::Log(channelAsString, lc::logging::LogLevel::LOG_LEVEL_WARNING, 0, GT_SOURCE_INFO, format, __VA_ARGS__)

#define GT_ERROR(channelAsString, format, ...) \
lc::logging::Log(channelAsString, lc::logging::LogLevel::LOG_LEVEL_ERROR, 0, GT_SOURCE_INFO, format, __VA_ARGS__)

#define GT_FATAL(channelAsString, format, ...) \
lc::logging::Log(channelAsString, lc::logging::LogLevel::LOG_LEVEL_FATAL, 0, GT_SOURCE_INFO, format, __VA_ARGS__)
