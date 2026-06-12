import { useState, useEffect, useMemo } from 'preact/compat';
import { useLingui } from '@lingui/react';
import { Card, CardContent } from '@/components/ui/card';
import { Button } from '@/components/ui/button';
import Upload from '@/components/upload';
import JsonEditor from '@/components/json-editor';
import Image from '@/components/image';
import Loading from '@/components/loading';
import modelVerification, {
  type InferenceImageReq,
} from '@/services/api/modelverification';
import deviceTool from '@/services/api/deviceTool';
import { fileToBase64 } from '@/utils/index';
import {
  fixedScaleImg,
  proportionScaleImg,
  convertToJpeg,
} from '@/utils/renderCanvasImg';
import SvgIcon from '@/components/svg-icon';

type GaugeReading = {
  index: number;
  value: number;
};

function extractReadings(aiResult: unknown): GaugeReading[] {
  if (!aiResult || typeof aiResult !== 'object') return [];
  const r = aiResult as {
    poses?: Array<{
      index?: number;
      reading?: { value: number };
    }>;
  };
  if (!Array.isArray(r.poses)) return [];
  return r.poses
    .filter((p) => p.reading && typeof p.reading.value === 'number')
    .map((p, i) => ({
      index: p.index ?? i,
      value: p.reading!.value,
    }));
}

export default function ModelVerification() {
  const { i18n } = useLingui();
  const { inferenceImageReq } = modelVerification;
  const { getAiStatusReq } = deviceTool;
  const [aiModelName, setAiModelName] = useState('');
  const [aiStatus, setAiStatus] = useState('');
  const [inferenceLoading, setInferenceLoading] = useState(false);
  const [showInferenceImage, setShowInferenceImage] = useState(true);
  const [inferenceImage, setInferenceImage] = useState<string | null>(null);
  const [originalImage, setOriginalImage] = useState<string | null>(null);

  const [aiImgSize, setAiImgSize] = useState({
    width: 480,
    height: 480,
  });
  const initAiStatus = async () => {
    try {
      const res = await getAiStatusReq();
      setAiModelName(res.data.model.name);
      setAiStatus(res.data.model.status);
      setAiImgSize(() => ({
        width: res.data.model.input_width,
        height: res.data.model.input_height,
      }));
    } catch (error) {
      console.error('initAiStatus', error);
      throw error;
    }
  };
  useEffect(() => {
    initAiStatus();
  }, []);

  const acceptFileType = {
    'image/jpeg': [],
    'image/png': [],
    'image/jpg': [],
    'image/webp': [],
  };
  const fileSize = 1024 * 10 * 1024;
  const [jsonValue, setJsonValue] = useState<any>(null);
  const readings = useMemo(() => extractReadings(jsonValue), [jsonValue]);

  const onFileChange = async (file: File) => {
    try {
      setShowInferenceImage(true);
      setInferenceLoading(true);
      setOriginalImage(await fileToBase64(file));
      if (file.type !== 'image/jpeg') {
        const convertToJpegFile = await convertToJpeg(file);
        file = new File(
          [convertToJpegFile],
          file.name.replace(/\.[^/.]+$/, '.jpeg'),
          { type: 'image/jpeg' }
        );
      }

      const proportionScaledFile = await proportionScaleImg({
        file,
        maxSize: 720,
        quality: 0.85,
      });

      const fixedScaledFile = await fixedScaleImg({
        file,
        aiImgHeight: aiImgSize.height,
        aiImgWidth: aiImgSize.width,
        quality: 0.85,
      });

      const formData = new FormData();
      formData.append('ai_image', fixedScaledFile.scaledFile);
      formData.append('draw_image', proportionScaledFile.scaledFile);
      formData.append('ai_image_width', aiImgSize.width.toString());
      formData.append('ai_image_height', aiImgSize.height.toString());
      formData.append('ai_image_quality', '85%');
      formData.append(
        'draw_image_width',
        proportionScaledFile.width.toString()
      );
      formData.append(
        'draw_image_height',
        proportionScaledFile.height.toString()
      );
      formData.append('draw_image_quality', '85%');

      const inferenceImageRes = await inferenceImageReq(
        formData as unknown as InferenceImageReq
      );
      const base64img = inferenceImageRes.data.output_image;

      // Correctly set state - convert to complete data URL
      const imageUrl = base64img.startsWith('data:')
        ? base64img
        : `data:image/jpeg;base64,${base64img}`;
      setInferenceImage(imageUrl);
      setJsonValue(inferenceImageRes.data.ai_result);
    } catch (error) {
      console.error('Failed to upload image:', error);
      // toast.error(error as string)
    } finally {
      setInferenceLoading(false);
    }
  };
  const uploadBtnSlot = (
    <>
      <SvgIcon icon="upload" />
      {i18n._('common.reupload')}
    </>
  );
  const uploadSlot = () => (
    <div className="flex gap-2 justify-center mt-2">
      <Button
        variant="outline"
        className=" h-full"
        onClick={() => setShowInferenceImage(!showInferenceImage)}
      >
        {showInferenceImage && (
          <>
            <svg
              xmlns="http://www.w3.org/2000/svg"
              height="20px"
              viewBox="0 0 24 24"
              width="20px"
              fill="#272E3B"
            >
              <path
                d="M0 0h24v24H0zm0 0h24v24H0zm0 0h24v24H0zm0 0h24v24H0z"
                fill="none"
              />
              <path d="M12 7c2.76 0 5 2.24 5 5 0 .65-.13 1.26-.36 1.83l2.92 2.92c1.51-1.26 2.7-2.89 3.43-4.75-1.73-4.39-6-7.5-11-7.5-1.4 0-2.74.25-3.98.7l2.16 2.16C10.74 7.13 11.35 7 12 7zM2 4.27l2.28 2.28.46.46C3.08 8.3 1.78 10.02 1 12c1.73 4.39 6 7.5 11 7.5 1.55 0 3.03-.3 4.38-.84l.42.42L19.73 22 21 20.73 3.27 3 2 4.27zM7.53 9.8l1.55 1.55c-.05.21-.08.43-.08.65 0 1.66 1.34 3 3 3 .22 0 .44-.03.65-.08l1.55 1.55c-.67.33-1.41.53-2.2.53-2.76 0-5-2.24-5-5 0-.79.2-1.53.53-2.2zm4.31-.78l3.15 3.15.02-.16c0-1.66-1.34-3-3-3l-.17.01z" />
            </svg>
            {i18n._('common.close_preview')}
          </>
        )}
        {!showInferenceImage && (
          <>
            <svg
              xmlns="http://www.w3.org/2000/svg"
              height="20px"
              viewBox="0 0 24 24"
              width="20px"
              fill="#272E3B"
            >
              <path d="M0 0h24v24H0z" fill="none" />
              <path d="M12 4.5C7 4.5 2.73 7.61 1 12c1.73 4.39 6 7.5 11 7.5s9.27-3.11 11-7.5c-1.73-4.39-6-7.5-11-7.5zM12 17c-2.76 0-5-2.24-5-5s2.24-5 5-5 5 2.24 5 5-2.24 5-5 5zm0-8c-1.66 0-3 1.34-3 3s1.34 3 3 3 3-1.34 3-3-1.34-3-3-3z" />
            </svg>
            {i18n._('common.open_preview')}
          </>
        )}
      </Button>
      <Upload
        onFileChange={onFileChange}
        slot={uploadBtnSlot}
        className="flex flex-1 h-full justify-start"
        type="button"
        accept={acceptFileType}
        maxFiles={1}
        maxSize={fileSize}
        multiple={false}
      />
    </div>
  );
  return (
    <div className="flex flex-col items-center w-full min-h-full bg-gray-100 pt-4 gap-4">
      <div className="flex flex-wrap justify-start items-center  gap-2 sm:w-xl w-full md:px-0 px-4">
        {/* <span className="text-lg mr-4 text-text-primary">{i18n._('sys.model_verification.model_verification_title')}</span> */}
        {aiStatus === 'loaded' && (
          <div className="flex items-center">
            <span className="text-sm text-text-secondary">
              {i18n._('sys.model_verification.model_verification_subtitle')}：
            </span>
            <span className="text-sm text-text-primary">{aiModelName}</span>
          </div>
        )}
      </div>
      <div className="w-full flex-1 px-4 flex justify-center">
        <Card className="w-full sm:w-xl mb-4">
          <CardContent>
            {inferenceLoading ? (
              <div className="w-full md:min-h-[340px] min-h-[340px] mt-4 flex flex-col items-center justify-center">
                <Loading
                  placeholder={i18n._('sys.model_verification.loading')}
                />
              </div>
            ) : (
              <div className="w-full md:min-h-[340px] md:h-1/2 min-h-[100px] h-[350px] bg-gray-100 rounded-md border-1 border-gray-300 border-dashed  flex flex-col items-center justify-center">
                {aiStatus === 'loaded' && (
                  <>
                    {inferenceImage && (
                      <div className="relative w-full max-h-[300px] flex items-center justify-center">
                        <Image
                          src={
                            showInferenceImage
                              ? inferenceImage
                              : originalImage || ''
                          }
                          alt="inferenceImage"
                          className=" h-full  p-2 object-contain"
                        />
                        {showInferenceImage && readings.length > 0 && (
                          <div className="absolute bottom-2 left-1/2 -translate-x-1/2 flex gap-2 pointer-events-none">
                            {readings.map((r) => (
                              <span
                                key={r.index}
                                className="px-2 py-0.5 rounded bg-black/60 text-white text-sm font-semibold tabular-nums"
                              >
                                {r.value.toFixed(2)}%
                              </span>
                            ))}
                          </div>
                        )}
                      </div>
                    )}
                    {!inferenceImage && (
                      <Upload
                        onFileChange={onFileChange}
                        maxSize={fileSize}
                        className="w-full h-[340px] flex-1"
                        type="imgZone"
                        accept={acceptFileType}
                        maxFiles={1}
                        multiple={false}
                        minSize={0}
                      />
                    )}
                    {inferenceImage && (
                      <div className="flex flex-col justify-end mb-2">
                        {uploadSlot()}
                      </div>
                    )}
                  </>
                )}
                {aiStatus === 'unloaded' && (
                  <div className="w-full h-[340px] flex flex-col items-center justify-center gap-3">
                    <SvgIcon icon="ai_off" className="w-16 h-16 text-gray-400" />
                    <p className="text-sm text-text-secondary text-center">{i18n._('sys.model_verification.model_unloaded')}</p>
                  </div>
                )}
              </div>
            )}
            <JsonEditor
              className="w-full h-[350px] mt-4 relative"
              jsonValue={jsonValue}
              loading={inferenceLoading}
            />
          </CardContent>
        </Card>
      </div>
    </div>
  );
}
