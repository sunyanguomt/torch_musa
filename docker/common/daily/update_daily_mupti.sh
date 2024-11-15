#!/bin/bash
set -e

DATE=$(date +%Y%m%d)
mupti_path=./daily_mupti_${DATE}

GPU=$(mthreads-gmi -q -i 0 | grep "Product Name" | awk -F: '{print $2}' | tr -d '[:space:]')

ARCH="x86_64-ubuntu-mp_21"

if [ "$GPU" = "MTTS3000" ] || [ "$GPU" = "MTTS80" ] || [ "$GPU" = "MTTS80ES" ]; then
    ARCH="x86_64-ubuntu-mp_21"
elif [ "$GPU" = "MTTS4000" ]; then
    ARCH="x86_64-ubuntu-mp_22"
else
    echo -e "\033[31mThe output of mthreads-gmi -q -i 0 | grep \"Product Name\" | awk -F: '{print \$2}' | tr -d '[:space:]' is not correct! Now GPU ARCH is set to qy1 by default! \033[0m"
fi

yesterday_date=$(TZ=Asia/Shanghai date -d "yesterday" +%Y%m%d)

# TODO(the oss link will be updated later)
mupti_oss_link=${mupti_oss_link:-https://oss.mthreads.com/release-ci/computeQA/musa/MUPTI/MUPTI_torch_profiler.tar.gz}
if [ "${KUAE}" ]; then
    echo -e "\033[31mIn kuae env, then use mupti for kuae! \033[0m"
    mupti_oss_link=https://oss.mthreads.com/release-ci/computeQA/musa/MUPTI/MUPTI_torch_profiler_kuae.tar.gz
fi

wget --no-check-certificate ${mupti_oss_link} -P ${mupti_path}
tar -xvzf ${mupti_path}/MUPTI_*.tar.gz -C ${mupti_path}
pushd ${mupti_path}
apt update && apt install libncurses-dev
dpkg -i MUPTI-0.2.0-Linux.deb
popd
echo -e "\033[31mmupti update to the newest version! \033[0m"
rm -rf ${mupti_path}
rm -f MUPTI-0.2.0-Linux.deb
