import { Skeleton } from '@/components/ui/skeleton';
import { Card, CardContent } from '@/components/ui/card';
import { Separator } from '@/components/ui/separator';

export default function DeviceToolSkeleton() {
  // Mirror a Label + Select settings row (Select trigger is h-9).
  const selectRow = (labelW: string, valueW: string) => (
    <div className="flex items-center justify-between">
      <Skeleton className={`h-4 ${labelW}`} />
      <Skeleton className={`h-9 ${valueW}`} />
    </div>
  );

  // Mirror a Label + Switch row (Switch is h-6 w-11).
  const switchRow = (labelW: string) => (
    <div className="flex items-center justify-between">
      <Skeleton className={`h-4 ${labelW}`} />
      <Skeleton className="h-6 w-11 rounded-full" />
    </div>
  );

  return (
    <div>
      {/* Camera settings title */}
      <Skeleton className="h-4 w-24 mb-2 mt-4" />

      {/* Camera settings card */}
      <Card className="bg-gray-50 mb-4">
        <CardContent className="">
          <div className="w-full h-full">
            {/* Model row: label + model name + upload button */}
            <div className="flex items-center justify-between flex-wrap gap-2">
              <Skeleton className="h-4 w-14" />
              <div className="flex items-center gap-2 flex-wrap">
                <Skeleton className="h-4 w-28" />
                <Skeleton className="h-9 w-20" />
              </div>
            </div>
            <Separator className="my-2" />
            {/* Power */}
            {selectRow('w-12', 'w-28')}
            <Separator className="my-2" />
            {/* Work mode */}
            {selectRow('w-12', 'w-28')}
            <Separator className="my-2" />
            {/* Sys clock */}
            {selectRow('w-16', 'w-32')}
            <Separator className="my-2" />
            {/* Media stream mode row */}
            <div className="flex items-center justify-between">
              <Skeleton className="h-4 w-28" />
              <Skeleton className="h-9 w-20" />
            </div>
            {/* Media stream config box */}
            <div className="border border-gray-200 border-solid p-4 rounded-md mt-2">
              <div className="flex justify-between gap-2 mb-2 flex-wrap">
                <Skeleton className="h-4 w-24" />
                <Skeleton className="h-4 w-24" />
              </div>
              <Separator className="mb-2" />
              <div className="flex items-center justify-between gap-2 mb-2">
                <Skeleton className="h-4 w-12" />
                <Skeleton className="h-9 flex-1" />
              </div>
              <div className="flex items-center justify-between gap-2 mb-2">
                <Skeleton className="h-4 w-16" />
                <Skeleton className="h-9 flex-1" />
              </div>
              <div className="flex justify-end mt-2">
                <Skeleton className="h-9 w-16" />
              </div>
            </div>
          </div>
        </CardContent>
      </Card>

      {/* Trigger title */}
      <Skeleton className="h-4 w-16 mb-2" />

      {/* Trigger card */}
      <Card className="bg-gray-50">
        <CardContent className="">
          <div className="w-full h-full">
            {/* PIR trigger */}
            {switchRow('w-16')}
            <Separator className="my-2" />
            {/* Remote control */}
            {switchRow('w-20')}
            <Separator className="my-2" />
            {/* Schedule */}
            {switchRow('w-16')}
          </div>
        </CardContent>
      </Card>
    </div>
  );
}
