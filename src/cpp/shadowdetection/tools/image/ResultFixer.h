#ifndef __RESULT_FIXER_H__ 
#define __RESULT_FIXER_H__

#include "typedefs.h"
#include "opencv2/core/core.hpp"

namespace shadowdetection{
    namespace tools{
        namespace image{
            
            class ResultFixer{
            private:                
                uchar lThresh;
                bool useThresh;
                bool useSky;
                
                void init() throw(SDException&);
            protected:
            public:
                ResultFixer();
                virtual ~ResultFixer();
                
                void applyThreshholds(  cv::Mat& image, const cv::Mat& originalImage, 
                                        const cv::Mat& hlsImage) throw(SDException&);
            };
            
        }
    }
}

#endif
