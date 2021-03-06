#ifndef __IPREDICTION_H__
#define __IPREDICTION_H__

#include "core/util/Matrix.h"
#include "typedefs.h"
#include <string>

namespace core{
    namespace util{
        namespace prediction{
         
            class IPrediction{                
            private:                
            protected:
            public:                
                virtual void loadModel() throw(SDException&) = 0;
                virtual unsigned char* predict( const core::util::Matrix<float>* imagePixelsParameters, 
                                const int& pixCount, const int& parameterCount) throw(SDException&) = 0;
                virtual bool hasLoadedModel() = 0;
            };
            
        }
    }
}

#endif
