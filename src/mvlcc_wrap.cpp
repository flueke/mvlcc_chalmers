#include <mesytec-mvlc/mesytec-mvlc.h>

#include <mvlcc_wrap.h>

using namespace mesytec::mvlc;

struct mvlcc
{
	mesytec::mvlc::CrateConfig config;
	mesytec::mvlc::MVLC mvlc;
	mesytec::mvlc::eth::MVLC_ETH_Interface *ethernet;
};

int readout_eth(eth::MVLC_ETH_Interface *a_eth, uint8_t *a_buffer,
    size_t *bytes_transferred);
int send_empty_request(MVLC *a_mvlc);

mvlcc_t
mvlcc_make_mvlc_from_crate_config(const char *configname)
{
	auto m = new mvlcc();
	std::ifstream config(configname);
	if(!config.is_open()) {
		printf("Could not open file '%s'\n", configname);
	}
	m->config = crate_config_from_yaml(config);
	m->mvlc = make_mvlc(m->config);
	m->ethernet = dynamic_cast<eth::MVLC_ETH_Interface *>(
	    m->mvlc.getImpl());
	return m;
}

mvlcc_t
mvlcc_make_mvlc(const char *urlstr)
{
	auto m = new mvlcc();
	m->mvlc = make_mvlc(urlstr);
	m->ethernet = dynamic_cast<eth::MVLC_ETH_Interface *>(
	    m->mvlc.getImpl());
	return m;
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
		abort();
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
mvlcc_init_readout(mvlcc_t a_mvlc)
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
		abort();
	}

	m->ethernet->resetPipeAndChannelStats();

	send_empty_request(&m->mvlc);

	auto ec = setup_readout_triggers(m->mvlc, m->config.triggers);
	if (ec) {
		printf("setup_readout_triggers: '%s'\n", ec.message().c_str());
		abort();
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
		abort();
	}

	return 0;
}

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
			abort();
		}
		if (ec == MVLCErrorCode::ShortRead) {
			printf("Short read.\n");
			abort();
		}
		rc = ec.value();
		if (rc != 0) {
			printf("'%s'\n", result.ec.message().c_str());
			abort();
		}
		if (result.leftoverBytes()) {
			printf("Leftover bytes. Bailing out!\n");
			abort();
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
		abort();
	}

	printf("Transferred %lu bytes\n", bytes_transferred);

	*a_buffer += bytes_transferred;

	return rc;
}

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
    abort();
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
    abort();
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

int mvlcc_vme_block_read(mvlcc_t a_mvlc, uint32_t address, uint32_t *buffer, size_t bufferInSize,
  size_t *bufferOutSize, struct MvlccBlockReadParams params)
{
	assert(buffer);
	assert(bufferOutSize);

	auto m = static_cast<struct mvlcc *>(a_mvlc);
	auto &mvlc = m->mvlc;

	const u16 maxTransfers = bufferInSize / (vme_amods::is_mblt_mode(params.amod) ? 2 : 1);

	std::vector<u32> localData;
	localData.reserve(bufferInSize/sizeof(u32));

	std::error_code ec;

	if (!params.swap)
		ec = mvlc.vmeBlockRead(address, params.amod, maxTransfers, localData, params.fifo);
	else
		ec = mvlc.vmeBlockReadSwapped(address, params.amod, maxTransfers, localData, params.fifo);

	if (ec)
		return ec.value();

	const auto end = std::min(localData.begin() + bufferInSize, localData.end());
	std::copy(localData.begin(), end, buffer);
	*bufferOutSize = end - localData.begin();

	return 0;
}

void mvlcc_set_global_log_level(const char *levelName)
{
	spdlog::warn("{} {}", __PRETTY_FUNCTION__, levelName);
	set_global_log_level(spdlog::level::from_str(levelName));
}
