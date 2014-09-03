#ifndef __SHADOW_DETECTION_PROCESSOR_H__
#define __SHADOW_DETECTION_PROCESSOR_H__

#include "core/process/IProcessor.h"
#include "core/util/Singleton.h"
#include "core/util/rtti/RTTI.h"

namespace shadowdetection{
    namespace process{
        
        class ShadowDetectionProcessor : public core::process::IProcessor, 
                public core::util::Singleton<ShadowDetectionProcessor>{
            friend class core::util::Singleton<ShadowDetectionProcessor>;
        PREPARE_REGISTRATION(ShadowDetectionProcessor)
        private:
        protected:
            ShadowDetectionProcessor();
        public:
            virtual ~ShadowDetectionProcessor();
            virtual void init() throw (SDException&);
            virtual void process(int argc, char **argv);
        };
        
    }
}

#endif