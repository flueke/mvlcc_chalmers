#include <mvlcc_wrap.h>

#include <mesytec-mvlc/mesytec-mvlc.h>
#include <string.h>

using namespace mesytec::mvlc;

// used to limit all strndup() calls. This includes json and yaml data too, so
// keep it large.
static const size_t STR_MAX_SIZE = 1u << 20;

struct mvlcc
{
	mesytec::mvlc::CrateConfig config;
	mesytec::mvlc::MVLC mvlc;
	mesytec::mvlc::eth::MVLC_ETH_Interface *ethernet;
	mesytec::mvlc::usb::MVLC_USB_Interface *usb;
	std::vector<u32> bltWorkBuffer;
};

int readout_eth(eth::MVLC_ETH_Interface *a_eth, uint8_t *a_buffer,
    size_t *bytes_transferred);
int send_empty_request(MVLC *a_mvlc);

static mvlcc_t make_mvlcc(const MVLC &mvlc, const CrateConfig &crateConfig = {})
{
	auto ret = std::make_unique<mvlcc>();
	ret->mvlc = mvlc;
	ret->ethernet = dynamic_cast<eth::MVLC_ETH_Interface *>(ret->mvlc.getImpl());
	ret->usb = dynamic_cast<usb::MVLC_USB_Interface *>(ret->mvlc.getImpl());
	ret->config = crateConfig;
	return ret.release();
}

mvlcc_t
mvlcc_make_mvlc_from_crate_config(const char *configname)
{
	auto m = new mvlcc();
	try
	{
		std::ifstream config(configname);
		if(!config.is_open()) {
			printf("Could not open file '%s'\n", configname);
			return m;
		}
		m->config = crate_config_from_yaml(config);
		m->mvlc = make_mvlc(m->config);
		m->ethernet = dynamic_cast<eth::MVLC_ETH_Interface *>(m->mvlc.getImpl());
		m->usb = dynamic_cast<usb::MVLC_USB_Interface *>(m->mvlc.getImpl());
	}
	catch (const std::runtime_error &e)
	{
		printf("Error reading crate config: %s\n", e.what());
	}
	return m;
}

mvlcc_t
mvlcc_make_mvlc(const char *urlstr)
{
	return make_mvlcc(make_mvlc(urlstr));
}

mvlcc_t mvlcc_make_mvlc_eth(const char *host)
{
	auto m = new mvlcc();
	m->mvlc = make_mvlc_eth(host);
	m->ethernet = dynamic_cast<eth::MVLC_ETH_Interface *>(
	    m->mvlc.getImpl());
	return m;
}

mvlcc_t mvlcc_make_mvlc_usb_from_index(int index)
{
	auto m = new mvlcc();
	m->mvlc = make_mvlc_usb(index);
	m->ethernet = nullptr;
	return m;
}

mvlcc_t mvlcc_make_mvlc_usb_from_serial(const char *serial)
{
	auto m = new mvlcc();
	m->mvlc = make_mvlc_usb(serial);
	m->ethernet = nullptr;
	return m;
}

void
mvlcc_free_mvlc(mvlcc_t a_mvlc)
{
	auto m = static_cast<struct mvlcc *>(a_mvlc);
	m->ethernet = nullptr;
	m->usb = nullptr;
	delete m;
}

int
mvlcc_connect(mvlcc_t a_mvlc)
{
	int rc;
	auto m = static_cast<struct mvlcc *>(a_mvlc);

	/* cancel ongoing readout when connecting */
	m->mvlc.setDisableTriggersOnConnect(true);

	auto ec = m->mvlc.connect();
	rc = ec.value();
	return rc;
}

int
mvlcc_stop(mvlcc_t a_mvlc)
{
	auto m = static_cast<struct mvlcc *>(a_mvlc);

	/* perhaps try this a couple of times */
	auto ec = disable_daq_mode_and_triggers(m->mvlc);
	if (ec) {
		printf("'%s'\n", ec.message().c_str());
		return 1;
	}

	return 0;
}

void
mvlcc_disconnect(mvlcc_t a_mvlc)
{
	auto m = static_cast<struct mvlcc *>(a_mvlc);
	m->mvlc.disconnect();
}

int
mvlcc_init_readout(mvlcc_t a_mvlc, int)
{
	int rc;
	auto m = static_cast<struct mvlcc *>(a_mvlc);

	assert(m->ethernet);

	auto result = init_readout(m->mvlc, m->config, {});

	printf("mvlcc_init_readout\n");
	// std::cout << "init_readout result = " << result.init << std::endl;

	rc = result.ec.value();
	if (rc != 0) {
		printf("init_readout: '%s'\n", result.ec.message().c_str());
		return rc;
	}

	if (m->ethernet)
	 {
		m->ethernet->resetPipeAndChannelStats();
		send_empty_request(&m->mvlc);
	}

	auto ec = setup_readout_triggers(m->mvlc, m->config.triggers);
	if (ec) {
		printf("setup_readout_triggers: '%s'\n", ec.message().c_str());
		return ec.value();
	}

	return rc;
}

int
send_empty_request(MVLC *a_mvlc)
{
	size_t bytesTransferred = 0;

	static const uint32_t empty_request[2] = {
		0xf1000000, 0xf2000000
	};

	auto ec = a_mvlc->getImpl()->write(Pipe::Data,
	    reinterpret_cast<const uint8_t *>(empty_request),
	    2 * sizeof(uint32_t), bytesTransferred);

	printf("send_empty_request: bytesTransferred = %lu\n", bytesTransferred);

	if (ec) {
		printf("Failure writing empty request.\n");
		return ec.value();
	}

	return 0;
}

#if 0 // Kept here for now, but use mvlcc_readout() instead which can do both ETH and USB.
int
readout_eth(eth::MVLC_ETH_Interface *a_eth, uint8_t *a_buffer,
    size_t a_buffer_len, size_t *a_bytes_transferred)
{
	int rc = 0;
	size_t total_bytes = 0;
	uint8_t *buffer = a_buffer;
	size_t bytes_free = a_buffer_len;
	int cycle = 0;

	printf("  readout_eth: start, bytes_free = %lu\n",
	    bytes_free);
	while (bytes_free >= eth::JumboFrameMaxSize)
	{
		auto result = a_eth->read_packet(Pipe::Data, buffer,
		    bytes_free);
		auto ec = result.ec;
		if (ec == ErrorType::ConnectionError) {
			printf("Connection error.\n");
			return ec.value();
		}
		if (ec == MVLCErrorCode::ShortRead) {
			printf("Short read.\n");
			return ec.value();
		}
		rc = ec.value();
		if (rc != 0) {
			printf("'%s'\n", result.ec.message().c_str());
			return ec.value();
		}
		if (result.leftoverBytes()) {
			printf("Leftover bytes. Bailing out!\n");
			return ec.value();
		}
		buffer += result.bytesTransferred;
		bytes_free -= result.bytesTransferred;
		total_bytes += result.bytesTransferred;

		printf("  readout_eth: cycle = %d, bytes_free = %lu, bytes_tranferred = %d, total_bytes = %lu\n",
		    cycle, bytes_free, result.bytesTransferred, total_bytes);

		++cycle;
	}

	*a_bytes_transferred = total_bytes;
	return rc;
}

int
mvlcc_readout_eth(mvlcc_t a_mvlc, uint8_t **a_buffer, size_t bytes_free)
{
	int rc;
	size_t bytes_transferred;
	uint8_t *buffer;
	auto m = static_cast<struct mvlcc *>(a_mvlc);

	buffer = *a_buffer;

	printf("mvlcc_readout_eth: a_buffer@%p, bytes_free = %lu\n", (void *)*a_buffer, bytes_free);

	rc = readout_eth(m->ethernet, buffer, bytes_free, &bytes_transferred);
	if (rc != 0) {
		printf("Failure in readout_eth %d\n", rc);
		return rc;
	}

	printf("Transferred %lu bytes\n", bytes_transferred);

	*a_buffer += bytes_transferred;

	return rc;
}
#endif

mvlcc_addr_width_t
mvlcc_addr_width_from_arg(uint8_t modStr)
{
  mvlcc_addr_width_t mode = mvlcc_A_ERR;
  if (modStr == 16) {
    mode = mvlcc_A16;
  } else if (modStr == 24) {
    mode = mvlcc_A24;
  } else if (modStr == 32) {
    mode = mvlcc_A32;
  } else {
    fprintf(stderr, "Invalid address width: %d\n", modStr);
  }

  return mode;
}

mvlcc_data_width_t
mvlcc_data_width_from_arg(uint8_t modStr)
{
  mvlcc_data_width_t mode = mvlcc_D_ERR;
  if (modStr == 16) {
    mode = mvlcc_D16;
  } else if (modStr == 32) {
    mode = mvlcc_D32;
  } else {
    fprintf(stderr, "Invalid data width: %d\n", modStr);
  }

  return mode;
}

int
mvlcc_single_vme_read(mvlcc_t a_mvlc, uint32_t address, uint32_t * value, uint8_t  amod, uint8_t dataWidth)
{
  int rc;

  auto m = static_cast<struct mvlcc *>(a_mvlc);

  //  mesytec::mvlc::VMEDataWidth m_width = static_cast<mesytec::mvlc::VMEDataWidth>(dataWidth);
  // mesytec::mvlc::u32 * m_value = (mesytec::mvlc::u32 *) value;

  uint8_t mode = mvlcc_addr_width_from_arg(amod);
  uint8_t dWidth = mvlcc_data_width_from_arg(dataWidth);
  mesytec::mvlc::VMEDataWidth m_width = static_cast<mesytec::mvlc::VMEDataWidth>(dWidth);

  auto ec = m->mvlc.vmeRead(address, *value, mode, m_width);
  // auto ec = m->mvlc.vmeRead(address, *m_value, amod, VMEDataWidth::D16);
  rc = ec.value();
  if (rc != 0) {
    printf("Failure in vmeRead %d (%s)\n", rc, ec.message().c_str());
	return rc;
  }

  // printf("\nvalue = %x\n", *value);

  return rc;
}

int
mvlcc_single_vme_write(mvlcc_t a_mvlc, uint32_t address, uint32_t value, uint8_t amod, uint8_t dataWidth)
{
  int rc;

  auto m = static_cast<struct mvlcc *>(a_mvlc);

  uint8_t mode = mvlcc_addr_width_from_arg(amod);
  uint8_t dWidth = mvlcc_data_width_from_arg(dataWidth);
  mesytec::mvlc::VMEDataWidth m_width = static_cast<mesytec::mvlc::VMEDataWidth>(dWidth);

  auto ec = m->mvlc.vmeWrite(address, value, mode, m_width);
  rc = ec.value();
  if (rc != 0) {
    printf("Failure in vmeWrite %d\n", rc);
	return rc;
  }
  //
  return rc;
}

int mvlcc_register_read(mvlcc_t a_mvlc, uint16_t address, uint32_t *value)
{
	auto m = static_cast<struct mvlcc *>(a_mvlc);
	auto ec = m->mvlc.readRegister(address, *value);
	return ec.value();
}

int mvlcc_register_write(mvlcc_t a_mvlc, uint16_t address, uint32_t value)
{
	auto m = static_cast<struct mvlcc *>(a_mvlc);
	auto ec = m->mvlc.writeRegister(address, value);
	return ec.value();
}

static char error_messages[static_cast<size_t>(mesytec::mvlc::MVLCErrorCode::ErrorCodeMax)][256];

const char *mvlcc_strerror(int errnum)
{
	if (errnum < static_cast<int>(mesytec::mvlc::MVLCErrorCode::ErrorCodeMax))
	{
		if (!error_messages[errnum][0])
		{
			auto ec = mesytec::mvlc::make_error_code(static_cast<mesytec::mvlc::MVLCErrorCode>(errnum));
			strncpy(error_messages[errnum], ec.message().c_str(), 255);
		}

		return error_messages[errnum];
	}

	return "<unknown error>";
}

int mvlcc_is_mvlc_valid(mvlcc_t a_mvlc)
{
	auto m = static_cast<struct mvlcc *>(a_mvlc);
	return m->mvlc.isValid();
}

int mvlcc_is_ethernet(mvlcc_t a_mvlc)
{
	auto m = static_cast<struct mvlcc *>(a_mvlc);
	return m->ethernet != nullptr;
}

int mvlcc_is_usb(mvlcc_t a_mvlc)
{
	auto m = static_cast<struct mvlcc *>(a_mvlc);
	return m->usb != nullptr;
}

int mvlcc_set_daq_mode(mvlcc_t a_mvlc, bool enable)
{
	auto m = static_cast<struct mvlcc *>(a_mvlc);
	std::error_code ec;
	if (enable)
		ec = mesytec::mvlc::enable_daq_mode(m->mvlc);
	else
		ec = mesytec::mvlc::disable_daq_mode(m->mvlc);
	return ec.value();
}

namespace
{
// Direct VME block read execution support code:
//
// The vmeBlockRead(Swapped)() methods where never intended to be used for
// readouts. They return the raw response, including MVLC frame headers
// (basically the USB framing format). The code in mvlcc_vme_block_read() reads
// into a work buffer, then post-processes the result to get rid of the headers.

struct end_of_frame: public std::exception {};

// Helper structure keeping track of the number of words left in a MVLC
// style data frame.
struct FrameParseState
{
	explicit FrameParseState(u32 frameHeader = 0)
		: header(frameHeader)
		, wordsLeft(extract_frame_info(frameHeader).len)
	{}

	FrameParseState(const FrameParseState &) = default;
	FrameParseState &operator=(const FrameParseState &o) = default;

	inline explicit operator bool() const { return wordsLeft; }
	inline FrameInfo info() const { return extract_frame_info(header); }

	inline void consumeWord()
	{
		if (wordsLeft == 0)
			throw end_of_frame();
		--wordsLeft;
	}

	inline void consumeWords(size_t count)
	{
		if (wordsLeft < count)
			throw end_of_frame();

		wordsLeft -= count;
	}

	u32 header;
	u16 wordsLeft;
};

u32 consume_one(std::basic_string_view<u32> &input)
{
	assert(!input.empty());

	auto result = input[0];
	input.remove_prefix(1);
	return result;
}

struct BltPostProcessState
{
	FrameParseState curStackFrame;
	FrameParseState curBlockFrame;
};

int post_process_blt_data(const std::vector<u32> &src, util::span<uint32_t> dest, size_t &wordsCopied)
{
	// Input structure from directly executed block reads:
	//  0xF3  outer stack frame header
	//    0x??  reference word that was added by the MVLC library code. Same as a "marker" command in a readout script.
	//    0xF5  first block read frame header.
	// [0xF9  optional stack continuation frames]
	//   [0xF5  optional continuations of the block read frame]

	if (src.size() < 3)
		return -1;

	std::basic_string_view<u32> input(src.data(), src.size());
	BltPostProcessState state;

	state.curStackFrame = FrameParseState(consume_one(input));
	consume_one(input); // reference word
	state.curBlockFrame = FrameParseState(consume_one(input));

	const auto outEnd = std::end(dest);
	auto outIter = std::begin(dest);
	wordsCopied = 0u;

	try
	{

		while (!input.empty())
		{
			if (auto stackFrameType = get_frame_type(state.curStackFrame.header);
				stackFrameType != frame_headers::StackFrame && stackFrameType != frame_headers::StackContinuation)
			{
				spdlog::error("post_process_blt_data(): expected StackFrame or StackContinuation frame header, got 0x{:08X}", state.curStackFrame.header);
				return -1;
			}

			if (auto blockFrameType = get_frame_type(state.curBlockFrame.header);
				blockFrameType != frame_headers::BlockRead)
			{
				spdlog::error("post_process_blt_data(): expected BlockRead frame header, got 0x{:08X}", state.curBlockFrame.header);
				return -1;
			}

			while (!input.empty() && state.curBlockFrame && outIter < outEnd)
			{
				*outIter++ = consume_one(input);
				state.curBlockFrame.consumeWord();
				++wordsCopied;
			}

			// The inner block frame does not have the continue flag set, so we're done.
			if (!(extract_frame_flags(state.curBlockFrame.header) & frame_flags::Continue))
				break;

			if (input.size() < 2) // need at least two words: outer 0xF9 StackContinuation and inner 0xF5 BlockRead
			{
				spdlog::error("post_process_blt_data(): input.size()={} (less than 2) -> result=-1", input.size());
				return -1;
			}

			state.curStackFrame = FrameParseState(consume_one(input));
			state.curBlockFrame	= FrameParseState(consume_one(input));
		}
	}
	catch (const end_of_frame &e)
	{
		return -1; // should not happen, data format corrupted
	}

	spdlog::trace("post_process_blt_data(): wordsCopied={}", wordsCopied);

	return 0;
}

}

int mvlcc_vme_block_read(mvlcc_t a_mvlc, uint32_t address, uint32_t *buffer, size_t sizeIn,
  size_t *sizeOut, struct MvlccBlockReadParams params)
{
	assert(buffer);
	assert(sizeOut);

	auto m = static_cast<struct mvlcc *>(a_mvlc);
	auto &mvlc = m->mvlc;

	const u16 maxTransfers = sizeIn / (vme_amods::is_mblt_mode(params.amod) ? 2 : 1);
	std::error_code ec;

	m->bltWorkBuffer.clear();

	if (vme_amods::is_mblt_mode(params.amod) && params.swap)
	{
		ec = mvlc.vmeBlockReadSwapped(address, params.amod, maxTransfers, m->bltWorkBuffer, params.fifo);
	}
	else
	{
		ec = mvlc.vmeBlockRead(address, params.amod, maxTransfers, m->bltWorkBuffer, params.fifo);
	}

	log_buffer(default_logger(), spdlog::level::debug, m->bltWorkBuffer,
		fmt::format("vmeBlockRead() (result={}, {}) raw data", ec.value(), ec.message()), 10);

	*sizeOut = 0;

	if (!ec || ec != MVLCErrorCode::VMEBusError)
	{
		util::span<uint32_t> dest(buffer, sizeIn);

		if (post_process_blt_data(m->bltWorkBuffer, dest, *sizeOut) != 0)
		{
			spdlog::warn("post_process_blt_data() failed, wordsCopied={}", *sizeOut);
		}
	}

	log_buffer(default_logger(), spdlog::level::debug, std::basic_string_view<u32>(buffer, *sizeOut),
		fmt::format("vmeBlockRead() (result={}, {}) post processed data", ec.value(), ec.message()), 10);

	return ec.value();
}

void mvlcc_set_global_log_level(const char *levelName)
{
	set_global_log_level(spdlog::level::from_str(levelName));
}

void mvlcc_print_mvlc_cmd_counters(FILE *out, mvlcc_t a_mvlc)
{
	assert(out);
	assert(a_mvlc);

	auto m = static_cast<struct mvlcc *>(a_mvlc);
	auto &mvlc = m->mvlc;
	const auto counters = mvlc.getCmdPipeCounters();

	fprintf(out, fmt::format("super txs: totalTxs={}, retries={}, cmd txs: totalTxs={}, retries={}, execRequestsLost={}, execResponsesLost={}",
		counters.superTransactionCount, counters.superTransactionRetries,
		counters.stackTransactionCount, counters.stackTransactionRetries,
		counters.stackExecRequestsLost, counters.stackExecResponsesLost).c_str());

	if (m->ethernet)
	{
		const auto cmdStats = m->ethernet->getPipeStats()[0];
		fprintf(out, fmt::format(", eth: lostPackets={}", cmdStats.lostPackets).c_str());
	}
}

void *mvlcc_get_mvlc_object(mvlcc_t a_mvlc)
{
	assert(a_mvlc);

	auto m = static_cast<struct mvlcc *>(a_mvlc);
	return &m->mvlc;
}

// Helpers for the intptr_t holding structures. T is the *_t typedefed struct, D
// is the concrete struct type.

template<typename T, typename D>
D *set_d(T &t, D *d)
{
	t.d = reinterpret_cast<intptr_t>(d);
	return d;
}

template<typename D, typename T>
D *get_d(T &t)
{
	return reinterpret_cast<D *>(t.d);
}

// Base to hold memory for the strerror() functions.
struct mvlcc_error_buffer
{
	std::string errorString;
};

struct mvlcc_command: public mvlcc_error_buffer
{
	mesytec::mvlc::StackCommand cmd;
};

int mvlcc_command_from_string(mvlcc_command_t *cmdp, const char *str)
{
	auto d = set_d(*cmdp, new mvlcc_command);

	try
	{
		d->cmd = mesytec::mvlc::stack_command_from_string(str);
		return 0;
	}
	catch(const std::exception& e)
	{
		d->errorString = e.what();
		return -1;
	}
}

void mvlcc_command_destroy(mvlcc_command_t *cmd)
{
	delete get_d<mvlcc_command>(*cmd);
	cmd->d = 0;
}

const char *mvlcc_command_strerror(mvlcc_command_t cmd)
{
	auto d = get_d<mvlcc_command>(cmd);
	return d->errorString.c_str();
}

char *mvlcc_command_to_string(mvlcc_command_t cmd)
{
	auto d = get_d<mvlcc_command>(cmd);
	return strndup(mesytec::mvlc::to_string(d->cmd).c_str(), STR_MAX_SIZE);
}

uint32_t mvlcc_command_get_vme_address(mvlcc_command_t cmd)
{
	auto d = get_d<mvlcc_command>(cmd);
	return d->cmd.address;
}

void mvlcc_command_set_vme_address(mvlcc_command_t cmd, uint32_t address)
{
	auto d = get_d<mvlcc_command>(cmd);
	d->cmd.address = address;
}

void mvlcc_command_add_to_vme_address(mvlcc_command_t cmd, uint32_t offset)
{
	auto d = get_d<mvlcc_command>(cmd);
	d->cmd.address += offset;
}

int mvlcc_run_command(mvlcc_t a_mvlc, mvlcc_command_t cmd, uint32_t *buffer, size_t size_in, size_t *size_out)
{
	auto m = static_cast<struct mvlcc *>(a_mvlc);
	auto d_cmd = get_d<mvlcc_command>(cmd);
	auto result = mesytec::mvlc::run_command(m->mvlc, d_cmd->cmd);
	if (result.ec)
		spdlog::warn("run_command() failed: cmd={}, ec={}", mesytec::mvlc::to_string(d_cmd->cmd), result.ec.message());
	else
		spdlog::info("run_command() ok: cmd={}, response={:#010x}", mesytec::mvlc::to_string(d_cmd->cmd), fmt::join(result.response, ", "));
	const auto end = std::begin(result.response) + std::min(size_in, result.response.size());
	std::copy(std::begin(result.response), end, buffer);
	if (size_out)
		*size_out = std::distance(std::begin(result.response), end);
	return result.ec.value();
}

struct mvlcc_command_list: public mvlcc_error_buffer
{
	mesytec::mvlc::StackCommandBuilder cmdList;
};

mvlcc_command_list_t mvlcc_command_list_create(void)
{
	mvlcc_command_list_t result = {};
	set_d(result, new mvlcc_command_list);
	return result;
}

void mvlcc_command_list_destroy(mvlcc_command_list_t *cmd_list)
{
	delete get_d<mvlcc_command_list>(*cmd_list);
	cmd_list->d = 0;
}

void mvlcc_command_list_clear(mvlcc_command_list_t cmd_list)
{
	get_d<mvlcc_command_list>(cmd_list)->cmdList.clear();
}

size_t mvlcc_command_list_total_size(mvlcc_command_list_t cmd_list)
{
	return get_d<mvlcc_command_list>(cmd_list)->cmdList.commandCount();
}

size_t mvlcc_command_list_begin_module_group(mvlcc_command_list_t cmd_list, const char *name)
{
	auto d = get_d<mvlcc_command_list>(cmd_list);
	d->cmdList.beginGroup(name);
	return d->cmdList.getGroupCount() - 1;
}

size_t mvlcc_command_list_get_module_group_count(mvlcc_command_list_t cmd_list)
{
	return get_d<mvlcc_command_list>(cmd_list)->cmdList.getGroupCount();
}

const char *mvlcc_command_list_get_module_group_name(mvlcc_command_list_t cmd_list, size_t index)
{
	auto d = get_d<mvlcc_command_list>(cmd_list);
	if (index < d->cmdList.getGroups().size())
		return d->cmdList.getGroup(index).name.c_str();

	return nullptr;
}

int mvlcc_command_list_add_command(mvlcc_command_list_t cmd_list, const char *cmd)
{
	auto d = get_d<mvlcc_command_list>(cmd_list);

	try
	{
		d->cmdList.addCommand(mesytec::mvlc::stack_command_from_string(cmd));
		return 0;
	}
	catch (const std::exception &e)
	{
		d->errorString = e.what();
		return -1;
	}
}

char *mvlcc_command_list_to_yaml(mvlcc_command_list_t cmd_list)
{
	auto d = get_d<mvlcc_command_list>(cmd_list);
	return strndup(mesytec::mvlc::to_yaml(d->cmdList).c_str(), STR_MAX_SIZE);
}

char *mvlcc_command_list_to_json(mvlcc_command_list_t cmd_list)
{
	auto d = get_d<mvlcc_command_list>(cmd_list);
	return strndup(mesytec::mvlc::to_json(d->cmdList).c_str(), STR_MAX_SIZE);
}

char *mvlcc_command_list_to_text(mvlcc_command_list_t cmd_list)
{
	auto d = get_d<mvlcc_command_list>(cmd_list);
	std::string buffer;
	for (const auto &cmd: d->cmdList.getCommands())
	{
		buffer += mesytec::mvlc::to_string(cmd) + "\n";
	}

	return strndup(buffer.c_str(), STR_MAX_SIZE);
}

int mvlcc_command_list_from_yaml(mvlcc_command_list_t *cmd_listp, const char *str)
{
	*cmd_listp = mvlcc_command_list_create();
	auto d = get_d<mvlcc_command_list>(*cmd_listp);

	try
	{
		d->cmdList = mesytec::mvlc::stack_command_builder_from_yaml(str);
		return 0;
	}
	catch(const std::exception& e)
	{
		d->errorString = e.what();
		return -1;
	}
}

int mvlcc_command_list_from_json(mvlcc_command_list_t *cmd_listp, const char *str)
{
	*cmd_listp = mvlcc_command_list_create();
	auto d = get_d<mvlcc_command_list>(*cmd_listp);

	try
	{
		d->cmdList = mesytec::mvlc::stack_command_builder_from_json(str);
		return 0;
	}
	catch(const std::exception& e)
	{
		d->errorString = e.what();
		return -1;
	}
}

int mvlcc_command_list_from_text(mvlcc_command_list_t *cmd_listp, const char *str)
{
	*cmd_listp = mvlcc_command_list_create();
	auto d = get_d<mvlcc_command_list>(*cmd_listp);

	try
	{
		mesytec::mvlc::StackCommandBuilder builder;
		std::string line;
		std::istringstream iss(str);
		while (std::getline(iss, line))
		{
			if (line.empty())
				continue;

			builder.addCommand(mesytec::mvlc::stack_command_from_string(line));
		}

		d->cmdList = std::move(builder);
		return 0;
	}
	catch(const std::exception& e)
	{
		d->errorString = e.what();
		return -1;
	}
}

int mvlcc_command_list_eq(mvlcc_command_list_t a, mvlcc_command_list_t b)
{
	auto da = get_d<mvlcc_command_list>(a);
	auto db = get_d<mvlcc_command_list>(b);
	return da->cmdList == db->cmdList;
}

const char *mvlcc_command_list_strerror(mvlcc_command_list_t cmd_list)
{
	auto d = get_d<mvlcc_command_list>(cmd_list);
	return d->errorString.c_str();
}

mvlcc_command_t mvlcc_command_list_get_command(mvlcc_command_list_t cmd_list, size_t index)
{
	auto d = get_d<mvlcc_command_list>(cmd_list);
	mvlcc_command_t result;
	auto d_cmd = set_d(result, new mvlcc_command);
	// superslow as the internal command vectors are flattened in getCommands()
	d_cmd->cmd = d->cmdList.getCommands().at(index);
	return result;
}

struct mvlcc_crateconfig: public mvlcc_error_buffer
{
	mesytec::mvlc::CrateConfig config;
};

mvlcc_crateconfig_t mvlcc_createconfig_create(void)
{
	mvlcc_crateconfig_t result = {};
	set_d(result, new mvlcc_crateconfig);
	return result;
}

void mvlcc_crateconfig_destroy(mvlcc_crateconfig_t *crateconfig)
{
	delete get_d<mvlcc_crateconfig>(*crateconfig);
	crateconfig->d = 0;
}

char *mvlcc_crateconfig_to_yaml(mvlcc_crateconfig_t crateconfig)
{
	auto d = get_d<mvlcc_crateconfig>(crateconfig);
	return strndup(mesytec::mvlc::to_yaml(d->config).c_str(), STR_MAX_SIZE);
}

char *mvlcc_crateconfig_to_json(mvlcc_crateconfig_t crateconfig)
{
	auto d = get_d<mvlcc_crateconfig>(crateconfig);
	return strndup(mesytec::mvlc::to_json(d->config).c_str(), STR_MAX_SIZE);
}

int mvlcc_crateconfig_from_yaml(mvlcc_crateconfig_t *crateconfigp, const char *str)
{
	auto d = set_d(*crateconfigp, new mvlcc_crateconfig);

	try
	{
		d->config = mesytec::mvlc::crate_config_from_yaml(str);
		return 0;
	}
	catch(const std::exception& e)
	{
		d->errorString = e.what();
		return -1;
	}
}

int mvlcc_crateconfig_from_json(mvlcc_crateconfig_t *crateconfigp, const char *str)
{
	auto d = set_d(*crateconfigp, new mvlcc_crateconfig);

	try
	{
		d->config = mesytec::mvlc::crate_config_from_json(str);
		return 0;
	}
	catch(const std::exception& e)
	{
		d->errorString = e.what();
		return -1;
	}
}

inline bool ends_with(std::string const & value, std::string const & ending)
{
    if (ending.size() > value.size()) return false;
    return std::equal(ending.rbegin(), ending.rend(), value.rbegin());
}

int mvlcc_crateconfig_from_file(mvlcc_crateconfig_t *crateconfigp, const char *filename)
{
	std::ifstream f(filename);
	std::stringstream buffer;
	buffer << f.rdbuf();

	if (ends_with(filename, ".json"))
	{
		return mvlcc_crateconfig_from_json(crateconfigp, buffer.str().c_str());
	}
	else
	{
		return mvlcc_crateconfig_from_yaml(crateconfigp, buffer.str().c_str());
	}
}

const char *mvlcc_crateconfig_strerror(mvlcc_crateconfig_t crateconfig)
{
	auto d = get_d<mvlcc_crateconfig>(crateconfig);
	return d->errorString.c_str();
}

mvlcc_t mvlcc_make_mvlc_from_crateconfig_t(mvlcc_crateconfig_t crateconfig)
{
	auto d = get_d<mvlcc_crateconfig>(crateconfig);
	return make_mvlcc(mesytec::mvlc::make_mvlc(d->config), d->config);
}

mvlcc_command_list_t mvlcc_crateconfig_get_readout_stack(
  mvlcc_crateconfig_t crateconfig, unsigned stackId)
{
	auto d_crateconfig = get_d<mvlcc_crateconfig>(crateconfig);

	if (stackId >= d_crateconfig->config.stacks.size())
		return {};

	auto cmd_list = mvlcc_command_list_create();
	auto d_cmd_list = get_d<mvlcc_command_list>(cmd_list);
	d_cmd_list->cmdList = d_crateconfig->config.stacks[stackId];

	return cmd_list;
}

int mvlcc_crateconfig_set_readout_stack(
  mvlcc_crateconfig_t crateconfig, unsigned stackId, mvlcc_command_list_t cmd_list)
{
	auto d_crateconfig = get_d<mvlcc_crateconfig>(crateconfig);
	auto d_cmd_list = get_d<mvlcc_command_list>(cmd_list);

	if (stackId >= mesytec::mvlc::stacks::ReadoutStackCount)
		return -1;

	d_crateconfig->config.stacks.resize(std::max(d_crateconfig->config.stacks.size(), static_cast<size_t>(stackId + 1)));
	d_crateconfig->config.stacks[stackId] = d_cmd_list->cmdList;

	return 0;
}

mvlcc_command_list_t mvlcc_crateconfig_get_mcst_daq_start(mvlcc_crateconfig_t crateconfig)
{
	auto d_crateconfig = get_d<mvlcc_crateconfig>(crateconfig);
	auto result = mvlcc_command_list_create();
	get_d<mvlcc_command_list>(result)->cmdList = d_crateconfig->config.mcstDaqStart;
	return result;
}

mvlcc_command_list_t mvlcc_crateconfig_get_mcst_daq_stop(mvlcc_crateconfig_t crateconfig)
{
	auto d_crateconfig = get_d<mvlcc_crateconfig>(crateconfig);
	auto result = mvlcc_command_list_create();
	get_d<mvlcc_command_list>(result)->cmdList = d_crateconfig->config.mcstDaqStop;
	return result;
}

int
mvlcc_init_readout2(mvlcc_t a_mvlc, mvlcc_crateconfig_t crateconfig)
{
	int rc;
	auto m = static_cast<struct mvlcc *>(a_mvlc);
	auto d_crateconfig = get_d<mvlcc_crateconfig>(crateconfig);

	assert(m->ethernet || m->usb);

	auto result = init_readout(m->mvlc, d_crateconfig->config, {});

	printf("mvlcc_init_readout\n");
	// std::cout << "init_readout result = " << result.init << std::endl;

	rc = result.ec.value();
	if (rc != 0) {
		printf("init_readout: '%s'\n", result.ec.message().c_str());
		return rc;
	}

	if (m->ethernet)
	 {
		m->ethernet->resetPipeAndChannelStats();
		send_empty_request(&m->mvlc);
	}

	return rc;
}

struct mvlcc_readout_context
{
	mesytec::mvlc::MVLC mvlc;
	mesytec::mvlc::ReadoutBuffer tmpBuffer;
};

mvlcc_readout_context_t mvlcc_readout_context_create(void)
{
	mvlcc_readout_context_t result = {};
	set_d(result, new mvlcc_readout_context);
	return result;
}

mvlcc_readout_context_t mvlcc_readout_context_create2(mvlcc_t a_mvlc)
{
	mvlcc_readout_context_t result = {};
	auto d = set_d(result, new mvlcc_readout_context);
	auto m = static_cast<struct mvlcc *>(a_mvlc);
	d->mvlc = m->mvlc;
	return result;
}

void mvlcc_readout_context_destroy(mvlcc_readout_context_t *ctx)
{
	delete get_d<mvlcc_readout_context>(*ctx);
	ctx->d = 0;
}

void mvlcc_readout_context_set_mvlc(mvlcc_readout_context_t ctx, mvlcc_t a_mvlc)
{
	auto d_ctx = get_d<mvlcc_readout_context>(ctx);
	auto m = static_cast<struct mvlcc *>(a_mvlc);
	d_ctx->mvlc = m->mvlc;
}

int mvlcc_readout(mvlcc_readout_context_t ctx, uint8_t *dest, size_t bytes_free, size_t *bytes_used, int timeout_ms)
{
	auto d_ctx = get_d<mvlcc_readout_context>(ctx);

	auto [ec, bytesRead] = mesytec::mvlc::readout(d_ctx->mvlc, d_ctx->tmpBuffer,
		{ dest, bytes_free }, std::chrono::milliseconds(timeout_ms));
	if (bytes_used)
		*bytes_used = bytesRead;
	return ec.value();
}

mvlcc_const_span_t mvlcc_module_data_get_prefix(mvlcc_module_data_t md)
{
  mvlcc_const_span_t result = {md.data_span.data, md.prefix_size};
  return result;
}

mvlcc_const_span_t mvlcc_module_data_get_dynamic(mvlcc_module_data_t md)
{
  mvlcc_const_span_t result = {md.data_span.data + md.prefix_size, md.dynamic_size};
  return result;
}

mvlcc_const_span_t mvlcc_module_data_get_suffix(mvlcc_module_data_t md)
{
  mvlcc_const_span_t result = {md.data_span.data + md.prefix_size + md.dynamic_size, md.suffix_size};
  return result;
}

int mvlcc_module_data_check_consistency(mvlcc_module_data_t md)
{
    uint64_t partSum = md.prefix_size + md.dynamic_size + md.suffix_size;
    int sumOk = partSum == md.data_span.size;
    // Note: cannot test the opposite: the current dynamicSize can be 0 but
    // hasDynamic can be true at the same time, e.g. from empty block reads.
    int dynOk = md.dynamic_size > 0 ? md.has_dynamic : 1;
    return sumOk && dynOk;
}

struct mvlcc_readout_parser: public mvlcc_error_buffer
{
  CrateConfig crateConfig;
  void *cUserContext;
  event_data_callback_t *cEventData;
  system_event_callback_t *cSystemEvent;
  readout_parser::ReadoutParserCallbacks parserCallbacks;
  readout_parser::ReadoutParserState readoutParser;
  readout_parser::ReadoutParserCounters parserCounters;
  std::vector<mvlcc_module_data_t> cModuleData;
};

static void event_data_internal(void *userContext, int crateIndex, int eventIndex,
	const readout_parser::ModuleData *moduleDataList, unsigned moduleCount)
{
	auto d = reinterpret_cast<mvlcc_readout_parser *>(userContext);
	d->cModuleData.resize(moduleCount);

	for (size_t mi=0; mi<moduleCount; ++mi)
	{
		d->cModuleData[mi].data_span = { moduleDataList[mi].data.data, moduleDataList[mi].data.size };
		d->cModuleData[mi].prefix_size = moduleDataList[mi].prefixSize;
		d->cModuleData[mi].dynamic_size = moduleDataList[mi].dynamicSize;
		d->cModuleData[mi].suffix_size = moduleDataList[mi].suffixSize;
		d->cModuleData[mi].has_dynamic = moduleDataList[mi].hasDynamic;
	}

	d->cEventData(d->cUserContext, crateIndex, eventIndex, d->cModuleData.data(), moduleCount);
}

static void system_event_internal(void *userContext, int crateIndex,
	const u32 *header, u32 size)
{
	auto d = reinterpret_cast<mvlcc_readout_parser *>(userContext);
	d->cSystemEvent(d->cUserContext, crateIndex, { header, size });
}

int mvlcc_readout_parser_create(
  mvlcc_readout_parser_t *parserp,
  mvlcc_crateconfig_t crateconfig,
  void *userContext,
  event_data_callback_t *event_data_callback,
  system_event_callback_t *system_event_callback)
{
	auto d = set_d(*parserp, new mvlcc_readout_parser);

	try
	{
		d->crateConfig = get_d<mvlcc_crateconfig>(crateconfig)->config;
		d->cUserContext = userContext;
		d->cEventData = event_data_callback;
		d->cSystemEvent = system_event_callback;
		d->parserCallbacks.eventData = event_data_internal;
		d->parserCallbacks.systemEvent = system_event_internal;
		d->readoutParser = readout_parser::make_readout_parser(d->crateConfig.stacks, d);
		return 0;
	}
	catch (const std::exception &e)
	{
		d->errorString = e.what();
		return -1;
	}
}

void mvlcc_readout_parser_destroy(mvlcc_readout_parser_t *parser)
{
	delete get_d<mvlcc_readout_parser>(*parser);
	parser->d = 0;
}

mvlcc_parse_result_t mvlcc_readout_parser_parse_buffer(
  mvlcc_readout_parser_t parser,
  size_t linear_buffer_number,
  const uint32_t *buffer,
  size_t size)
{
	auto d = get_d<mvlcc_readout_parser>(parser);

	auto result = readout_parser::parse_readout_buffer(
		d->crateConfig.connectionType,
		d->readoutParser,
		d->parserCallbacks,
		d->parserCounters,
		linear_buffer_number, buffer, size);

	return static_cast<int>(result);
}

const char *mvlcc_parse_result_to_string(mvlcc_parse_result_t result)
{
	return readout_parser::get_parse_result_name(
		static_cast<readout_parser::ParseResult>(result));
}
