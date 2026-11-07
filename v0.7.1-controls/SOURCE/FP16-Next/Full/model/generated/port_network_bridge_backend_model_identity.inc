static constexpr size_t kPlanRows=152,kPlanBuffers=495,kPlanPasses=225,kPlanOutputs=1,kPlanHistory=2;
static constexpr const char* kPreImageName="ioport_0_ioport_images";
static constexpr const char* kPostImageName="ioport_70_ioport_images";
static constexpr unsigned kPostBlock=70,kSplitLast=30,kDecoderBlock=39;
struct NRSwinBlock {unsigned block,channels;};
static constexpr NRSwinBlock kSwinBlocks[]={{1,32},{2,32},{3,32},{4,32},{5,64},{6,64},{7,64},{8,64},{9,128},{10,128},{11,128},{12,128},{13,128},{14,128},{15,256},{16,256},{17,256},{18,256},{19,256},{20,256},{21,256},{22,256},{48,256},{49,256},{50,256},{51,256},{52,256},{53,256},{54,256},{55,256},{56,128},{57,128},{58,128},{59,128},{60,128},{61,128},{62,64},{63,64},{64,64},{65,64},{66,32},{67,32},{68,32},{69,32}};
static constexpr unsigned kDownsampleBlocks[]={4,8,14,22};
static constexpr unsigned kUpsampleBlocks[]={48,56,62,66};
static constexpr unsigned kVitBlocks[]={31,32,33,34,35,36,37,38};
