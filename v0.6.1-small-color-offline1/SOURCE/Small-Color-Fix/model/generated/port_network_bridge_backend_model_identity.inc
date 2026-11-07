static constexpr size_t kPlanRows=76,kPlanBuffers=222,kPlanPasses=108,kPlanOutputs=1,kPlanHistory=2;
static constexpr const char* kPreImageName="ioport_0_ioport_images";
static constexpr const char* kPostImageName="ioport_30_ioport_images";
static constexpr unsigned kPostBlock=30,kSplitLast=10,kDecoderBlock=19;
struct NRSwinBlock {unsigned block,channels;};
static constexpr NRSwinBlock kSwinBlocks[]={{1,32},{2,32},{3,64},{4,64},{5,128},{6,128},{7,256},{8,256},{22,256},{23,256},{24,128},{25,128},{26,64},{27,64},{28,32},{29,32}};
static constexpr unsigned kDownsampleBlocks[]={2,4,6,8};
static constexpr unsigned kUpsampleBlocks[]={22,24,26,28};
static constexpr unsigned kVitBlocks[]={11,12,13,14,15,16,17,18};
